#define main existingPolicyRegressionMain
#include "../../tests/policy-core.test.cpp"
#undef main
int main(){
 fixture();phase="STARTING";state("WAIT_PATROL_CONFIG");sessionId=271;geometryConfirmed=false;
 policyInput("DATA 271 CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");
 printf("normal config geometryConfirmed=%d\n",geometryConfirmed);CHECK(geometryConfirmed);
 fixture();phase="INTERACTION";initiator="CAT_TOUCH";mode="UART";lastUnoAt=tick;
 recoverLink();retryLink();retryLink();CHECK(linkRecoveryAttempts==3);stopped();lastUnoAt=tick;policyTick();
 auto stopSession=sessionId;policyInput("DONE "+String(stopSession)+" SESSION_STOPPED");CHECK(action=="LINK_RECOVERY_CONFIG");auto configId=sessionId;
 policyInput("DATA "+String(configId)+" CONFIG WHEEL_MM=67.5 TRACK_MM=120.0 SLOTS_PER_REV=20 SAFE_LEFT_CM=1.0 SAFE_RIGHT_CM=1.0");
 printf("recovery config geometryConfirmed=%d, state=%s\n",geometryConfirmed,action.c_str());CHECK(!geometryConfirmed);
 policyInput("DONE "+String(configId));printf("recovery after valid DONE: phase=%s, action=%s, fault=%s\n",phase.c_str(),action.c_str(),lastFault.c_str());
 CHECK(lastFault=="Uno link recovery failed after 3 attempts");CHECK(phase=="STOPPED");return 0;
}
