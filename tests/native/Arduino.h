#pragma once
#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <type_traits>
#define PI 3.14159265358979323846
class String {
 std::string value;
public:
 String()=default;
 String(const char *s):value(s?s:""){}
 String(const std::string &s):value(s){}
 template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0> String(T n):value(std::to_string(n)){}
 String(float n,unsigned char decimals=2){std::ostringstream o;o<<std::fixed<<std::setprecision(decimals)<<n;value=o.str();}
 String(double n,unsigned char decimals=2):String(float(n),decimals){}
 const char *c_str()const{return value.c_str();}
 unsigned length()const{return value.size();}
 int indexOf(const char *s)const{auto p=value.find(s);return p==std::string::npos?-1:int(p);}
 int indexOf(char c)const{auto p=value.find(c);return p==std::string::npos?-1:int(p);}
 bool startsWith(const char *s)const{return value.rfind(s,0)==0;}
 bool endsWith(const char *s)const{size_t n=strlen(s);return value.size()>=n&&value.compare(value.size()-n,n,s)==0;}
 String substring(unsigned begin)const{return value.substr(begin);}
 String substring(unsigned begin,unsigned end)const{return value.substr(begin,end-begin);}
 float toFloat()const{return strtof(c_str(),nullptr);}
 long toInt()const{return strtol(c_str(),nullptr,10);}
 bool concat(const char *s,size_t n){value.append(s,n);return true;}
 bool concat(const char *s){value+=s;return true;}
 bool concat(char c){value+=c;return true;}
 String &operator+=(const String &s){value+=s.value;return *this;}
 char operator[](size_t n)const{return value[n];}
 friend String operator+(const String&a,const String&b){return a.value+b.value;}
 friend bool operator==(const String&a,const String&b){return a.value==b.value;}
 friend bool operator!=(const String&a,const String&b){return !(a==b);}
};
template<class T> T min(T a,T b){return a<b?a:b;}
template<class T> T max(T a,T b){return a>b?a:b;}
uint32_t millis();
uint32_t esp_random();
