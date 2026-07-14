#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char BYTE; typedef unsigned int UINT32; typedef int INT32;
typedef struct { unsigned short left,top,right,bottom; } R16;
static BYTE CLIP(INT32 X){return X>255?255:(X<0?0:(BYTE)X);}
static BYTE CCLIP(INT32 in,BYTE o){BYTE u=CLIP(in);int d=u>o?u-o:o-u;return d<30?o:u;}
static BYTE R_(INT32 Y,INT32 U,INT32 V){return CLIP((256*Y+403*(V-128))>>8);}
static BYTE G_(INT32 Y,INT32 U,INT32 V){return CLIP((256*Y-48*(U-128)-120*(V-128))>>8);}
static BYTE B_(INT32 Y,INT32 U,INT32 V){return CLIP((256*Y+475*(U-128))>>8);}
static void Luma(BYTE*S0,BYTE*S1,BYTE*S2,int s0,int s1,int s2,BYTE*D0,BYTE*D1,BYTE*D2,int ds,int W,int H){
 int hw=(W+1)/2,hh=(H+1)/2,x,y;
 for(y=0;y<H;y++)memcpy(D0+(size_t)y*ds,S0+(size_t)y*s0,W);
 for(y=0;y<hh;y++){BYTE*Um=S1+(size_t)y*s1,*Vm=S2+(size_t)y*s2;BYTE*pU=D1+(size_t)ds*(2*y),*pV=D2+(size_t)ds*(2*y),*pU1=D1+(size_t)ds*(2*y+1),*pV1=D2+(size_t)ds*(2*y+1);
  for(x=0;x<hw;x++){int a=2*x,b=2*x+1;pU[a]=Um[x];pV[a]=Vm[x];pU[b]=Um[x];pV[b]=Vm[x];pU1[a]=Um[x];pV1[a]=Vm[x];pU1[b]=Um[x];pV1[b]=Vm[x];}}}
static void ChV2(BYTE*s0,BYTE*s1,BYTE*s2,int S0,int S1,int S2,UINT32 nT,BYTE*D1,BYTE*D2,int ds,R16 r){
 UINT32 nW=r.right-r.left,nH=r.bottom-r.top,hw=(nW+1)/2,hh=(nH+1)/2,qw=(nW+3)/4,x,y;
 for(y=0;y<nH;y++){UINT32 yT=y+r.top;BYTE*pYaU=s0+(size_t)S0*yT+r.left/2,*pYaV=pYaU+nT/2;BYTE*pU=D1+(size_t)ds*yT+r.left,*pV=D2+(size_t)ds*yT+r.left;for(x=0;x<hw;x++){UINT32 o=2*x+1;pU[o]=*pYaU++;pV[o]=*pYaV++;}}
 for(y=0;y<hh;y++){BYTE*pUaU=s1+(size_t)S1*(y+r.top/2)+r.left/4,*pUaV=pUaU+nT/4,*pVaU=s2+(size_t)S2*(y+r.top/2)+r.left/4,*pVaV=pVaU+nT/4;BYTE*pU=D1+(size_t)ds*(2*y+1+r.top)+r.left,*pV=D2+(size_t)ds*(2*y+1+r.top)+r.left;for(x=0;x<qw;x++){pU[4*x]=*pUaU++;pV[4*x]=*pUaV++;pU[4*x+2]=*pVaU++;pV[4*x+2]=*pVaV++;}}}
static void RGB(BYTE*Y,BYTE*U,BYTE*V,int st,int W,int H,BYTE*rgb){int x,y;
 for(y=0;y+1<H;y+=2){BYTE*pY[2]={Y+(size_t)y*st,Y+(size_t)(y+1)*st},*pU[2]={U+(size_t)y*st,U+(size_t)(y+1)*st},*pV[2]={V+(size_t)y*st,V+(size_t)(y+1)*st};
  for(x=0;x+1<W;x+=2)for(int i=0;i<2;i++)for(int j=0;j<2;j++){BYTE yy=pY[i][x+j];INT32 u=pU[i][x+j],v=pV[i][x+j];if(i==0&&j==0){u=CCLIP(4*u-((INT32)pU[0][x+1]+pU[1][x]+pU[1][x+1]),pU[i][x+j]);v=CCLIP(4*v-((INT32)pV[0][x+1]+pV[1][x]+pV[1][x+1]),pV[i][x+j]);}BYTE*o=rgb+((size_t)(y+i)*W+(x+j))*3;o[0]=R_(yy,u,v);o[1]=G_(yy,u,v);o[2]=B_(yy,u,v);}}}
int main(int c,char**v){int W=atoi(v[3]),H=atoi(v[4]),cw=(W+15)&~15,ch=(H+15)&~15,chw=cw/2;
 size_t nv=(size_t)cw*ch*3/2;BYTE*M=malloc(nv),*A=malloc(nv);
 FILE*f=fopen(v[1],"rb");fread(M,1,nv,f);fclose(f);f=fopen(v[2],"rb");fread(A,1,nv,f);fclose(f);
 BYTE*mY=M,*mUV=M+(size_t)cw*ch,*aY=A,*aUV=A+(size_t)cw*ch;
 BYTE*mU=malloc((size_t)chw*ch/2),*mV=malloc((size_t)chw*ch/2),*aU=malloc((size_t)chw*ch/2),*aV=malloc((size_t)chw*ch/2);
 for(int cy=0;cy<ch/2;cy++)for(int j=0;j<chw;j++){mU[(size_t)cy*chw+j]=mUV[(size_t)cy*cw+2*j];mV[(size_t)cy*chw+j]=mUV[(size_t)cy*cw+2*j+1];aU[(size_t)cy*chw+j]=aUV[(size_t)cy*cw+2*j];aV[(size_t)cy*chw+j]=aUV[(size_t)cy*cw+2*j+1];}
 BYTE*dY=calloc((size_t)cw*ch,1),*dU=calloc((size_t)cw*ch,1),*dV=calloc((size_t)cw*ch,1);
 Luma(mY,mU,mV,cw,chw,chw,dY,dU,dV,cw,W,H);R16 r={0,0,W,H};ChV2(aY,aU,aV,cw,chw,chw,cw,dU,dV,cw,r);
 BYTE*rgb=calloc((size_t)W*H*3,1);RGB(dY,dU,dV,cw,W,H,rgb);
 FILE*o=fopen(v[5],"wb");fwrite(rgb,1,(size_t)W*H*3,o);fclose(o);return 0;}
