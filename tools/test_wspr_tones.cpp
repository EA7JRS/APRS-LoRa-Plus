// Prueba en PC del generador de tonos WSPR (Manuel - EA7JRS). Compilar: g++ -std=c++17 -Iinclude tools/test_wspr_tones.cpp src/wspr.cpp -o t && ./t
#include "wspr.h"
#include <cstdio>
#include <cmath>
int main(){
    int fails=0;
    for (int fine=0; fine<=1; fine++) for (int corr=-300; corr<=300; corr+=37) {
        WSPR::Tones t; t.begin(432301500.0,(double)corr,fine);
        double sumDev=0, maxDev=0; uint32_t prev[4]; double hzAvg[4];
        for (uint8_t s=0;s<4;s++){
            uint32_t symUs=682667;
            auto p=t.plan(s,0,symUs);
            if (p.switchUs>symUs) {printf("switch fuera\n");fails++;}
            double w0=p.first*WSPR::PLL_STEP_HZ, w1=p.second*WSPR::PLL_STEP_HZ;
            double avg = (p.second==p.first)? w0 : (w0*p.switchUs + w1*(symUs-p.switchUs))/symUs;
            hzAvg[s]=avg;
            double err=avg-t.hz(s,0);
            if (fine && fabs(err)>0.03*WSPR::PLL_STEP_HZ+0.001) {printf("fine err %.4f Hz sym %d corr %d\n",err,s,corr);fails++;}
            if (!fine && fabs(err)>0.5*WSPR::PLL_STEP_HZ+1e-6) {printf("round err %.4f\n",err);fails++;}
            if (p.second!=p.first && p.second!=p.first+1){printf("palabras no vecinas\n");fails++;}
            if (!fine && p.second!=p.first){printf("segundo tramo con fine=0\n");fails++;}
        }
        for (int s=1;s<4;s++){
            double sp=hzAvg[s]-hzAvg[s-1];
            double lim = fine? 0.06 : 1.0;
            if (fabs(sp-WSPR::TONE_SPACING)>lim){printf("spacing %.4f corr %d fine %d\n",sp,corr,fine);fails++;}
        }
    }
    // deriva: rampa centrada, media cero, pendiente correcta
    { WSPR::Tones a,b; a.begin(432301500.0,0,true,0.0); b.begin(432301500.0,0,true,-4.0);
      double sum=0; for(int i=0;i<162;i++) sum+=b.hz(1,i)-a.hz(1,i);
      double d0=b.hz(1,0)-a.hz(1,0), d1=b.hz(1,161)-a.hz(1,161);
      double slope=(d1-d0)/(161*8192.0/12000.0)*60.0;
      printf("media rampa %.4f Hz, pendiente %.3f Hz/min, extremos %.2f %.2f\n",sum/162,slope,d0,d1);
      if (fabs(sum/162)>0.01 || fabs(slope+4.0)>0.01) fails++;
      // plan con deriva sigue dando palabras vecinas y media correcta
      for(int i=0;i<162;i+=7){ auto p=b.plan(2,i,682667); double w0=p.first*WSPR::PLL_STEP_HZ,w1=p.second*WSPR::PLL_STEP_HZ;
        double avg=(p.second==p.first)?w0:(w0*p.switchUs+w1*(682667-p.switchUs))/682667.0; if(fabs(avg-b.hz(2,i))>0.04){printf("avg deriva %d\n",i);fails++;} } }
    // fine sigue en pie: separacion exacta del WSPR
    { int exp[7]={0,3,7,10,13,17,20};
      for(int i=0;i<7;i++) if (WSPR::powerIndex(exp[i])!=i){printf("powerIndex %d\n",exp[i]);fails++;}
      if (WSPR::powerIndex(-3)!=0||WSPR::powerIndex(25)!=6||WSPR::powerIndex(14)!=4){printf("powerIndex limites\n");fails++;} }
    printf("fails=%d\n",fails);
    return fails!=0;
}
