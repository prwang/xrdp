#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xrdp_avc444_convert.h"

typedef unsigned char BYTE;
typedef unsigned int UINT32;
typedef int INT32;
typedef struct { unsigned short left, top, right, bottom; } RECTANGLE_16;

static BYTE CLIP(INT32 X){ if(X>255)return 255; if(X<0)return 0; return (BYTE)X; }
static BYTE CONDITIONAL_CLIP(INT32 in, BYTE orig){
    BYTE out=CLIP(in); int d = out>orig ? out-orig : orig-out;
    return d<30 ? orig : out;
}
/* BT.601 full-range, exactly FreeRDP prim_internal.h */
static BYTE YUV2R(INT32 Y,INT32 U,INT32 V){ return CLIP((256*Y + 403*(V-128))>>8); }
static BYTE YUV2G(INT32 Y,INT32 U,INT32 V){ return CLIP((256*Y - 48*(U-128) - 120*(V-128))>>8); }
static BYTE YUV2B(INT32 Y,INT32 U,INT32 V){ return CLIP((256*Y + 475*(U-128))>>8); }

/* FreeRDP general_LumaToYUV444 (main frame: Y copy + main U/V replicated to 2x2) */
static void LumaToYUV444(const BYTE*S0,const BYTE*S1,const BYTE*S2,int sstep0,int sstep1,int sstep2,
                         BYTE*D0,BYTE*D1,BYTE*D2,int dstep,int W,int H){
    int hw=(W+1)/2, hh=(H+1)/2, x,y;
    for(y=0;y<H;y++) memcpy(D0+(size_t)y*dstep, S0+(size_t)y*sstep0, W);
    for(y=0;y<hh;y++){
        const BYTE*Um=S1+(size_t)y*sstep1, *Vm=S2+(size_t)y*sstep2;
        BYTE*pU=D1+(size_t)dstep*(2*y), *pV=D2+(size_t)dstep*(2*y);
        BYTE*pU1=D1+(size_t)dstep*(2*y+1), *pV1=D2+(size_t)dstep*(2*y+1);
        for(x=0;x<hw;x++){
            int x0=2*x,x1=2*x+1;
            pU[x0]=Um[x]; pV[x0]=Vm[x]; pU[x1]=Um[x]; pV[x1]=Vm[x];
            pU1[x0]=Um[x]; pV1[x0]=Vm[x]; pU1[x1]=Um[x]; pV1[x1]=Vm[x];
        }
    }
}
/* FreeRDP general_ChromaV2ToYUV444 (verbatim), nTotalWidth = aligned width */
static void ChromaV2(const BYTE*s0,const BYTE*s1,const BYTE*s2,int ss0,int ss1,int ss2,
                     UINT32 nTotalWidth, BYTE*D1,BYTE*D2,int dstep, RECTANGLE_16 roi){
    UINT32 nW=roi.right-roi.left,nH=roi.bottom-roi.top;
    UINT32 halfW=(nW+1)/2, halfH=(nH+1)/2, quaterW=(nW+3)/4, x,y;
    for(y=0;y<nH;y++){
        UINT32 yT=y+roi.top;
        const BYTE*pYaU=s0+(size_t)ss0*yT+roi.left/2; const BYTE*pYaV=pYaU+nTotalWidth/2;
        BYTE*pU=D1+(size_t)dstep*yT+roi.left; BYTE*pV=D2+(size_t)dstep*yT+roi.left;
        for(x=0;x<halfW;x++){ UINT32 odd=2*x+1; pU[odd]=*pYaU++; pV[odd]=*pYaV++; }
    }
    for(y=0;y<halfH;y++){
        const BYTE*pUaU=s1+(size_t)ss1*(y+roi.top/2)+roi.left/4; const BYTE*pUaV=pUaU+nTotalWidth/4;
        const BYTE*pVaU=s2+(size_t)ss2*(y+roi.top/2)+roi.left/4; const BYTE*pVaV=pVaU+nTotalWidth/4;
        BYTE*pU=D1+(size_t)dstep*(2*y+1+roi.top)+roi.left; BYTE*pV=D2+(size_t)dstep*(2*y+1+roi.top)+roi.left;
        for(x=0;x<quaterW;x++){ pU[4*x]=*pUaU++; pV[4*x]=*pUaV++; pU[4*x+2]=*pVaU++; pV[4*x+2]=*pVaV++; }
    }
}
/* FreeRDP DOUBLE_ROW reverse filter + BT.601 RGB */
static void YUV444ToRGB(const BYTE*Y,const BYTE*U,const BYTE*V,int step,int W,int H,BYTE*rgb){
    int x,y;
    for(y=0;y+1<H;y+=2){
        const BYTE*pY[2]={Y+(size_t)y*step,Y+(size_t)(y+1)*step};
        const BYTE*pU[2]={U+(size_t)y*step,U+(size_t)(y+1)*step};
        const BYTE*pV[2]={V+(size_t)y*step,V+(size_t)(y+1)*step};
        for(x=0;x+1<W;x+=2){
            for(int i=0;i<2;i++)for(int j=0;j<2;j++){
                BYTE yy=pY[i][x+j]; INT32 u=pU[i][x+j], v=pV[i][x+j];
                if(i==0&&j==0){
                    INT32 subU=(INT32)pU[0][x+1]+pU[1][x]+pU[1][x+1];
                    u=CONDITIONAL_CLIP(4*u-subU, pU[i][x+j]);
                    INT32 subV=(INT32)pV[0][x+1]+pV[1][x]+pV[1][x+1];
                    v=CONDITIONAL_CLIP(4*v-subV, pV[i][x+j]);
                }
                BYTE*o=rgb+((size_t)(y+i)*W+(x+j))*3;
                o[0]=YUV2R(yy,u,v); o[1]=YUV2G(yy,u,v); o[2]=YUV2B(yy,u,v);
            }
        }
    }
}

int main(int argc,char**argv){
    /* read source raw XRGB (bytes B,G,R,X per pixel) + dims from argv */
    int W=atoi(argv[2]), H=atoi(argv[3]);
    FILE*f=fopen(argv[1],"rb"); unsigned char*src=malloc((size_t)W*H*4);
    fread(src,1,(size_t)W*H*4,f); fclose(f);
    struct xrdp_avc444_conv*c=xrdp_avc444_conv_create(W,H);
    c->chroma_v2=1;
    xrdp_avc444_conv_update(c,src,W*4,W,H);
    int cw=c->coded_width, ch=c->coded_height, chw=cw/2;
    /* optional argv[5]=main.nv12 argv[6]=aux.nv12 : load (H.264-roundtripped)
       NV12 in place of the converter output. argv[7]="dump" dumps NV12 out. */
    if(argc>7 && strcmp(argv[7],"dump")==0){
        FILE*m=fopen(argv[5],"wb"); fwrite(c->main_nv12,1,c->nv12_size,m); fclose(m);
        FILE*a=fopen(argv[6],"wb"); fwrite(c->aux_nv12,1,c->nv12_size,a); fclose(a);
        printf("dumped NV12 %dx%d size %d\n",cw,ch,c->nv12_size); return 0;
    }
    if(argc>6 && strcmp(argv[5],"none")!=0){
        FILE*m=fopen(argv[5],"rb"); fread(c->main_nv12,1,c->nv12_size,m); fclose(m);
        FILE*a=fopen(argv[6],"rb"); fread(c->aux_nv12,1,c->nv12_size,a); fclose(a);
    }
    /* de-interleave main + aux NV12 -> planar */
    BYTE*mY=c->main_nv12, *mUV=c->main_nv12+(size_t)cw*ch;
    BYTE*aY=c->aux_nv12,  *aUV=c->aux_nv12 +(size_t)cw*ch;
    BYTE*mU=malloc((size_t)chw*(ch/2)),*mV=malloc((size_t)chw*(ch/2));
    BYTE*aU=malloc((size_t)chw*(ch/2)),*aV=malloc((size_t)chw*(ch/2));
    for(int cy=0;cy<ch/2;cy++)for(int j=0;j<chw;j++){
        mU[(size_t)cy*chw+j]=mUV[(size_t)cy*cw+2*j]; mV[(size_t)cy*chw+j]=mUV[(size_t)cy*cw+2*j+1];
        aU[(size_t)cy*chw+j]=aUV[(size_t)cy*cw+2*j]; aV[(size_t)cy*chw+j]=aUV[(size_t)cy*cw+2*j+1];
    }
    BYTE*dY=calloc((size_t)cw*ch,1),*dU=calloc((size_t)cw*ch,1),*dV=calloc((size_t)cw*ch,1);
    LumaToYUV444(mY,mU,mV,cw,chw,chw,dY,dU,dV,cw,W,H);
    /* roi origin from env ROI_L/ROI_T (default full-surface even 0,0) to test
       the per-region-rect parity hypothesis */
    int rl = getenv("ROI_L")?atoi(getenv("ROI_L")):0;
    int rt = getenv("ROI_T")?atoi(getenv("ROI_T")):0;
    RECTANGLE_16 roi={rl,rt,W,H};
    ChromaV2(aY,aU,aV,cw,chw,chw,cw,dU,dV,cw,roi);
    BYTE*rgb=calloc((size_t)W*H*3,1);
    YUV444ToRGB(dY,dU,dV,cw,W,H,rgb);
    FILE*o=fopen(argv[4],"wb"); fwrite(rgb,1,(size_t)W*H*3,o); fclose(o);
    printf("decoded %dx%d (coded %dx%d) -> %s\n",W,H,cw,ch,argv[4]);
    return 0;
}
