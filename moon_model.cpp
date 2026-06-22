#include "moon_model.hpp"
#include <cmath>
namespace { constexpr double P=3.14159265358979323846; double r(double x){return x*P/180.;} double n(double x){x=std::fmod(x,360.);return x<0?x+360.:x;} double d(double x,double y){return n(x-y+180.)-180.;} }
MoonPosition calculateMoonPosition(double jd, double sun) {
  double T=(jd-2451545.)/36525., L=n(218.3164477+481267.88123421*T), D=n(297.8501921+445267.1114034*T), M=n(357.5291092+35999.0502909*T), Q=n(134.9633964+477198.8675055*T), F=n(93.272095+483202.0175233*T), E=1-.002516*T-.0000074*T*T;
  auto s=[](double x){return std::sin(r(x));}; auto c=[](double x){return std::cos(r(x));};
  double dl=(6288774*s(Q)+1274027*s(2*D-Q)+658314*s(2*D)+213618*s(2*Q)-185116*E*s(M)-114332*s(2*F)+58793*s(2*D-2*Q)+57066*E*s(2*D-M-Q)+53322*s(2*D+Q))/1e6;
  double b=(5128122*s(F)+280602*s(Q+F)+277693*s(Q-F)+173237*s(2*D-F)+55413*s(2*D-Q+F)+46271*s(2*D-Q-F)+32573*s(2*D+F))/1e6;
  double dist=385000.56+(-20905355*c(Q)-3699111*c(2*D-Q)-2955968*c(2*D)-569925*c(2*Q)+48888*E*c(M)-246158*c(2*D-2*Q))/1000.;
  double lon=n(L+dl), e=r(23.439291-.0130042*T), br=r(b), lr=r(lon), dec=std::asin(std::sin(br)*std::cos(e)+std::cos(br)*std::sin(e)*std::sin(lr))*180./P, signedEl=d(lon,sun), el=std::abs(signedEl);
  return {lon,b,dist,dec,signedEl,el,(1-std::cos(r(el)))/2};
}
