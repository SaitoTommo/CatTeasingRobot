import test from "node:test";
import assert from "node:assert/strict";
import { touchResponsePlan } from "./decision-system.mjs";

test("each touch area can choose a flash and a separate prey, sound-light, or escape response", () => {
  for (const area of ["TOP", "LEFT", "RIGHT", "REAR"]) {
    const prey = touchResponsePlan(area, () => .1);
    const signal = touchResponsePlan(area, () => .3);
    const escape = touchResponsePlan(area, () => .9);
    assert.equal(prey.response, "PREY_STILL");
    assert.equal(prey.flashDuringTouch, true);
    assert.equal(signal.response, "SOUND_LIGHT_RESPONSE");
    assert.equal(escape.response, "FAST_STRAIGHT_ESCAPE");
    assert.equal(escape.flashDuringTouch, false);
  }
});

test("contact light precedes release; escape waits for release; prey collapses on contact and resumes patrol", async () => {
  const elements = new Map();
  const archivedMessages = [];
  const drawing = new Proxy({}, { get: () => () => {} });
  class Element {
    constructor(id = "") {
      this.id = id;
      this.value = ({ catScenarioBehavior: "STILL", attackArea: "TOP",
        catSpeedCmS: "20", attackHoldMs: "700", attackIntervalMs: "20000",
        tagStopCm: "20", wheelDiameterMm: "65", trackWidthMm: "120" })[id] ?? "";
      this.checked = false;
      this.children = [];
      this.listeners = new Map();
      this.classList = { toggle() {} };
      this.hidden = false;
      this.textContent = "";
      this.attributes = new Map();
    }
    get selectedOptions() { return [{ textContent: this.value }]; }
    get firstChild() { return this.children[0]; }
    get scrollHeight() { return this.children.length; }
    getContext() { return drawing; }
    addEventListener(type, listener) {
      const listeners = this.listeners.get(type) ?? [];
      listeners.push(listener);
      this.listeners.set(type, listeners);
    }
    dispatch(type, event = {}) {
      for (const listener of this.listeners.get(type) ?? [])
        listener({ target: this, preventDefault() {}, ...event });
    }
    click() { this.dispatch("click"); }
    setAttribute(name, value) { this.attributes.set(name, String(value)); }
    getAttribute(name) { return this.attributes.get(name) ?? null; }
    append(...children) {
      for (const child of children) { child.parent = this; this.children.push(child); }
    }
    remove() {
      if (this.parent?.id === "log") archivedMessages.push(this.children[2]?.textContent);
      const index = this.parent?.children.indexOf(this) ?? -1;
      if (index >= 0) this.parent.children.splice(index, 1);
    }
    replaceChildren() { this.children.length = 0; }
    getBoundingClientRect() { return { left: 0, top: 0, width: 1000, height: 600 }; }
    setPointerCapture() {}
  }
  const touchButton = new Element("topTouch");
  touchButton.dataset = { touch: "TOP" };
  globalThis.document = {
    visibilityState: "visible",
    getElementById(id) {
      if (!elements.has(id)) elements.set(id, new Element(id));
      return elements.get(id);
    },
    createElement() { return new Element(); },
    querySelectorAll(selector) { return selector === "[data-touch]" ? [touchButton] : []; },
    addEventListener() {},
  };
  let nextFrame = null;
  globalThis.requestAnimationFrame = callback => { nextFrame = callback; };
  const previousRandom = Math.random;
  Math.random = () => .5;
  try {
    await import("./app.mjs");
    const messages = () => [...archivedMessages,
      ...elements.get("log").children.map(row => row.children[2].textContent)];
    const runFramesUntil = async (predicate, timeoutMs = 8000) => {
      const deadline = performance.now() + timeoutMs;
      while (performance.now() < deadline && !predicate()) {
        const frame = nextFrame;
        assert.ok(frame, "simulation frame must remain scheduled");
        nextFrame = null;
        frame(performance.now());
        await new Promise(resolve => setTimeout(resolve, 16));
      }
      assert.ok(predicate(), messages().slice(-45).join(" | "));
    };

    elements.get("touchFlashPercent").value = "100";
    elements.get("touchPreyPercent").value = "0";
    elements.get("touchSoundLightPercent").value = "0";
    elements.get("touchSlowEscapePercent").value = "100";
    elements.get("touchFastEscapePercent").value = "0";
    elements.get("postEscapeWaitSeconds").value = "0";
    elements.get("touchDebounceMs").value = "0";
    elements.get("applyResponsePolicy").click();
    elements.get("startPatrol").click();
    const escapeStart = messages().length;
    touchButton.click();
    await runFramesUntil(() => messages().slice(escapeStart).some(m => m.includes("CONTACT_LIGHT area=TOP flash=true")));
    const early = messages().slice(escapeStart);
    const press = early.findIndex(m => m.includes("TOUCH area=TOP phase=PRESS"));
    const flash = early.findIndex(m => /CMD \d+ LED FLASH 220 /.test(m));
    const release = early.findIndex(m => m.includes("TOUCH area=TOP phase=RELEASE"));
    assert.ok(press >= 0 && flash > press && release < 0,
      `LED must flash while contact is held: ${early.join(" | ")}`);
    assert.ok(!early.some(m => /CMD \d+ (DRIVE|ROTATE) /.test(m)),
      "escape wheels must not start while touch is held");
    await runFramesUntil(() => messages().slice(escapeStart).some(m => m.includes("ATTACK_RESPONSE_PHASE escape_immediate")));
    const escapeLog = messages().slice(escapeStart);
    const released = escapeLog.findIndex(m => m.includes("TOUCH area=TOP phase=RELEASE"));
    const moving = escapeLog.findIndex(m => /CMD \d+ (DRIVE|ROTATE) /.test(m));
    assert.ok(released >= 0 && moving > released,
      `escape motion must follow release: ${escapeLog.slice(-35).join(" | ")}`);
    elements.get("stopPatrol").click();

    elements.get("touchPreyPercent").value = "100";
    elements.get("touchSlowEscapePercent").value = "0";
    elements.get("captureWaitMaxSeconds").value = "1";
    elements.get("applyResponsePolicy").click();
    elements.get("applyEndingPolicy").click();
    elements.get("startPatrol").click();
    const preyStart = messages().length;
    touchButton.click();
    await runFramesUntil(() => messages().slice(preyStart).some(m => m.includes("ENDING_OUTCOME_START outcome=PREY_STILL")));
    const preyLog = messages().slice(preyStart);
    assert.ok(preyLog.some(m => m.includes("CONTACT_LIGHT area=TOP flash=true")));
    assert.ok(!preyLog.some(m => m.includes("TOUCH area=TOP phase=RELEASE")),
      "prey collapse must begin while the cat is still touching");
    await runFramesUntil(() => messages().slice(preyStart).some(m => m.includes("PATROL_RESUME target_tag=")), 5000);
    assert.ok(messages().slice(preyStart).some(m => m.includes("DONE") && m.includes("SESSION_STARTED")),
      "after collapse the Uno session must reopen for patrol");
    elements.get("stopPatrol").click();

    elements.get("touchFlashPercent").value = "0";
    elements.get("touchPreyPercent").value = "0";
    elements.get("touchSoundLightPercent").value = "100";
    elements.get("touchSlowEscapePercent").value = "0";
    elements.get("touchFastEscapePercent").value = "0";
    elements.get("applyResponsePolicy").click();
    elements.get("startPatrol").click();
    const signalStart = messages().length;
    touchButton.click();
    await runFramesUntil(() => messages().slice(signalStart).some(m => m.includes("ATTACK_RESPONSE_PHASE sound_light_on_contact")));
    const signalLog = messages().slice(signalStart);
    const signalPress = signalLog.findIndex(m => m.includes("TOUCH area=TOP phase=PRESS"));
    const signalFlash = signalLog.findIndex(m => /CMD \d+ LED FLASH 220 /.test(m));
    const signalBeep = signalLog.findIndex(m => /CMD \d+ BEEP 180/.test(m));
    assert.ok(signalPress >= 0 && signalFlash > signalPress && signalBeep > signalPress,
      `sound and light must begin on contact: ${signalLog.join(" | ")}`);
    assert.ok(!signalLog.some(m => m.includes("TOUCH area=TOP phase=RELEASE")));
    assert.ok(!signalLog.some(m => /CMD \d+ (DRIVE|ROTATE) /.test(m)),
      "sound-light response must keep the wheels stopped during contact");
    elements.get("stopPatrol").click();
  } finally {
    Math.random = previousRandom;
  }
});
