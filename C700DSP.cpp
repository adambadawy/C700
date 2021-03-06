﻿//
//  C700DSP.cpp
//  C700
//
//  Created by osoumen on 2014/11/30.
//
//

#include "C700DSP.h"
#include "gauss.h"
//#include <iomanip>

#define filter1(a1)	(( a1 >> 1 ) + ( ( -a1 ) >> 5 ))
#define filter2(a1,a2)	(a1 + ( ( -( a1 + ( a1 >> 1 ) ) ) >> 5 ) - ( a2 >> 1 ) + ( a2 >> 5 ))
#define filter3(a1,a2)	(a1  + ( ( -( a1 + ( a1 << 2 ) + ( a1 << 3 ) ) ) >> 7 )  - ( a2 >> 1 )  + ( ( a2 + ( a2 >> 1 ) ) >> 4 ))

static const int	*G1 = &gauss[256];
static const int	*G2 = &gauss[512];
static const int	*G3 = &gauss[255];
static const int	*G4 = &gauss[0];

static const int	CNT_INIT = 0x7800;
static const int	ENVCNT[32]
= {
    0x0000, 0x000F, 0x0014, 0x0018, 0x001E, 0x0028, 0x0030, 0x003C,
    0x0050, 0x0060, 0x0078, 0x00A0, 0x00C0, 0x00F0, 0x0140, 0x0180,
    0x01E0, 0x0280, 0x0300, 0x03C0, 0x0500, 0x0600, 0x0780, 0x0A00,
    0x0C00, 0x0F00, 0x1400, 0x1800, 0x1E00, 0x2800, 0x3C00, 0x7800
};

static unsigned char silence_brr[] = {
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

//-----------------------------------------------------------------------------
void C700DSP::DSPState::Reset()
{
    ar = -1;
    dr = -1;
    sl = -1;
    sr = -1;
    vol_l = -200;
    vol_r = -200;
    ecen = false;
    ecenNotWrited = true;
    
	smp1=0;
	smp2=0;
	sampbuf[0]=0;
	sampbuf[1]=0;
	sampbuf[2]=0;
	sampbuf[3]=0;
	brrdata = silence_brr;
	loopPoint = 0;
	pitch = 0;
	memPtr = 0;
	headerCnt = 0;
	half = 0;
	envx = 0;
	end = 0;
	loop = 0;
	sampptr = 0;
	mixfrac=0;
	envcnt = CNT_INIT;
	envstate = RELEASE;
}

C700DSP::C700DSP() :
mNewADPCM( false ),
mUseRealEmulation( true )
{
    //Initialize
	mMainVolume_L = 127;
	mMainVolume_R = 127;
    mDirAddr = 0x200;
    mEchoVolL = 0;
    mEchoVolR = 0;
    mEchoStartAddr = 0xff;
    mEchoDelay = 0;
    mEchoFeedBack = 0;
    mEchoEnableWait = 0;
    /*
    mEchoEnableWait = 8000;
    gettimeofday(&mEchoChangeTime, NULL);
    mEchoChangeWaitusec = 250000;
     */
    mBrrStartAddr = 0x10000;
    mBrrEndAddr = 0;

    mDsp.setDeviceReadyFunc(onDeviceReady, this);
    mDsp.setDeviceExitFunc(onDeviceStop, this);
    mDsp.init();
}

C700DSP::~C700DSP()
{

}

void C700DSP::ResetVoice(int voice)
{
    mVoice[voice].Reset();
    if (voice < 8) {
        mDsp.WriteDsp(DSP_KOF, 1 << voice, false);
        //mDsp.WriteDsp(DSP_KOF, 0x00, false);
    }
}

void C700DSP::KeyOffAll()
{
    for (int i=0; i<kMaximumVoices; i++) {
        mVoice[i].Reset();
    }
    mDsp.WriteDsp(DSP_KOF, 0xff, false);
}

void C700DSP::ResetEcho()
{
    mEcho[0].Reset();
	mEcho[1].Reset();
    // エコー領域のRAMをリセット
    //mDsp.WriteDsp(DSP_EVOLL, 0, false);
    //mDsp.WriteDsp(DSP_EVOLR, 0, false);
    //mDsp.WriteDsp(DSP_EFB, 0, false);
    //mDsp.WriteDsp(DSP_FLG, 0x20, false);
    /*
    if (mEchoEnableWait < mEchoDelay * 480) {
        mEchoEnableWait = mEchoDelay * 480;
        gettimeofday(&mEchoChangeTime, NULL);
        mEchoChangeWaitusec = mEchoDelay * 16000;
    }
     */
}

void C700DSP::SetVoiceLimit(int value)
{
    mVoiceLimit = value;
}

void C700DSP::SetNewADPCM(bool value)
{
    mNewADPCM = value;
}

void C700DSP::SetRealEmulation(bool value)
{
    mUseRealEmulation = value;
    if (value) {
        mDsp.EndMuteEmulation();
        if (!mDsp.IsHwAvailable()) {
            mDsp.WriteRam(0x200, &mRam[0x200], 0x400);
        }
    }
    else {
        mDsp.StartMuteEmulation();
    }
}

void C700DSP::SetMainVolumeL(int value)
{
    if (mMainVolume_L != value) {
        mMainVolume_L = value;
        mDsp.WriteDsp(DSP_MVOLL, static_cast<unsigned char>(value & 0xff), false);
    }
}

void C700DSP::SetMainVolumeR(int value)
{
    if (mMainVolume_R != value) {
        mMainVolume_R = value;
        mDsp.WriteDsp(DSP_MVOLR, static_cast<unsigned char>(value & 0xff), false);
    }
}

void C700DSP::SetEchoVol_L( int value )
{
    if (mEchoVolL != value) {
        mEchoVolL = value & 0xff;
        mEcho[0].SetEchoVol( value );
        if (mEchoEnableWait <= 0) {
            mDsp.WriteDsp(DSP_EVOLL, static_cast<unsigned char>(mEchoVolL), false);
        }
    }
}

void C700DSP::SetEchoVol_R( int value )
{
    if (mEchoVolR != value) {
        mEchoVolR = value & 0xff;
        mEcho[1].SetEchoVol( value );
        if (mEchoEnableWait <= 0) {
            mDsp.WriteDsp(DSP_EVOLR, static_cast<unsigned char>(mEchoVolR), false);
        }
    }
}

void C700DSP::SetFeedBackLevel( int value )
{
    if (mEchoFeedBack != value) {
        mEchoFeedBack = value & 0xff;
        mEcho[0].SetFBLevel( value );
        mEcho[1].SetFBLevel( value );
        if (mEchoEnableWait <= 0) {
            mDsp.WriteDsp(DSP_EFB, static_cast<unsigned char>(mEchoFeedBack), false);
        }
    }
}

void C700DSP::SetDelayTime( int value )
{
    //std::cout << "edl:0x" << std::hex << std::setw(2) << std::setfill('0') << value << std::endl;
    //assert(mEchoDelay != value);
    if (mEchoDelay != value) {
        mEchoDelay = value & 0xff;
        mEchoStartAddr = 0x06;  // DIRの直後
        
        mEcho[0].SetDelayTime( value );
        mEcho[1].SetDelayTime( value );
        //mDsp.WriteDsp(DSP_EVOLL, 0, false);
        //mDsp.WriteDsp(DSP_EVOLR, 0, false);
        //mDsp.WriteDsp(DSP_EFB, 0, false);
        //mDsp.WriteDsp(DSP_FLG, 0x20, false);
        //mDsp.WriteDsp(DSP_EDL, 0, false);
        
        //mDsp.WriteDsp(DSP_ESA, static_cast<unsigned char>(mEchoStartAddr), false);
        mDsp.WriteDsp(DSP_EDL, static_cast<unsigned char>(mEchoDelay), false);
        /*
        mEchoEnableWait = 8000; // 250ms
        gettimeofday(&mEchoChangeTime, NULL);
        mEchoChangeWaitusec = 250000;
         */
    }
}

void C700DSP::SetFIRTap( int tap, int value )
{
	mEcho[0].SetFIRTap(tap, value);
	mEcho[1].SetFIRTap(tap, value);
    mDsp.WriteDsp(DSP_FIR + 0x10*tap, static_cast<unsigned char>(value & 0xff), false);
}

void C700DSP::KeyOffVoice(int v)
{
    mVoice[v].envstate = RELEASE;
    mDsp.WriteDsp(DSP_KOF, static_cast<unsigned char>(0x01 << v), false);
    mDsp.WriteDsp(DSP_KOF, 0, false);  // 本当に必要？
}

void C700DSP::KeyOnVoice(int v)
{
    mVoice[v].memPtr = 0;
    mVoice[v].headerCnt = 0;
    mVoice[v].half = 0;
    mVoice[v].envx = 0;
    mVoice[v].end = 0;
    mVoice[v].sampptr = 0;
    mVoice[v].mixfrac = 3 * 4096;
    mVoice[v].envcnt = CNT_INIT;
    mVoice[v].envstate = ATTACK;
    mDsp.WriteDsp(DSP_KON, static_cast<unsigned char>(0x01 << v), false);
}

void C700DSP::KeyOnVoiceFlg(int flg)
{
    for (int v=0; v<kMaximumVoices; v++) {
        if (flg & (1 << v)) {
            mVoice[v].memPtr = 0;
            mVoice[v].headerCnt = 0;
            mVoice[v].half = 0;
            mVoice[v].envx = 0;
            mVoice[v].end = 0;
            mVoice[v].sampptr = 0;
            mVoice[v].mixfrac = 3 * 4096;
            mVoice[v].envcnt = CNT_INIT;
            mVoice[v].envstate = ATTACK;
        }
    }
    mDsp.WriteDsp(DSP_KON, static_cast<unsigned char>(flg & 0xff), false);
}

void C700DSP::SetAR(int v, int value)
{
    if (mVoice[v].ar != value) {
        mVoice[v].ar = value;
        unsigned char data = 0x80;
        data |= mVoice[v].ar & 0x0f;
        data |= (mVoice[v].dr & 0x07) << 4;
        if (v < 8) {
            mDsp.WriteDsp(DSP_ADSR + 0x10*v, data, false);
        }
    }
}

void C700DSP::SetDR(int v, int value)
{
    if (mVoice[v].dr != value) {
        mVoice[v].dr = value;
        unsigned char data = 0x80;
        data |= mVoice[v].ar & 0x0f;
        data |= (mVoice[v].dr & 0x07) << 4;
        if (v < 8) {
            mDsp.WriteDsp(DSP_ADSR + 0x10*v, data, false);
        }
    }
}

void C700DSP::SetSL(int v, int value)
{
    if (mVoice[v].sl != value) {
        mVoice[v].sl = value;
        unsigned char data = 0;
        data |= mVoice[v].sr & 0x1f;
        data |= (mVoice[v].sl & 0x07) << 5;
        if (v < 8) {
            mDsp.WriteDsp(DSP_ADSR+1 + 0x10*v, data, false);
        }
    }
}

void C700DSP::SetSR(int v, int value)
{
    if (mVoice[v].sr != value) {
        mVoice[v].sr = value;
        unsigned char data = 0;
        data |= mVoice[v].sr & 0x1f;
        data |= (mVoice[v].sl & 0x07) << 5;
        if (v < 8) {
            mDsp.WriteDsp(DSP_ADSR+1 + 0x10*v, data, false);
        }
    }
}

void C700DSP::SetVol_L(int v, int value)
{
    if (mVoice[v].vol_l != value) {
        mVoice[v].vol_l = value;
        if (v < 8) {
            mDsp.WriteDsp(DSP_VOL + 0x10*v, static_cast<unsigned char>(value), false);
        }
    }
}

void C700DSP::SetVol_R(int v, int value)
{
    if (mVoice[v].vol_r != value) {
        mVoice[v].vol_r = value;
        if (v < 8) {
            mDsp.WriteDsp(DSP_VOL+1 + 0x10*v, static_cast<unsigned char>(value), false);
        }
    }
}

void C700DSP::SetPitch(int v, int value)
{
    if (mVoice[v].pitch != value) {
        mVoice[v].pitch = value;
        if (v < 8) {
            mDsp.WriteDsp(DSP_P + 0x10*v, static_cast<unsigned char>(value&0xff), false);
            mDsp.WriteDsp(DSP_P+1 + 0x10*v, static_cast<unsigned char>((value>>8)&0x3f), false);
        }
    }
}

void C700DSP::SetEchoOn(int v, bool isOn)
{
    if ((mVoice[v].ecen != isOn) || mVoice[v].ecenNotWrited) {
        mVoice[v].ecen = isOn;
        mVoice[v].ecenNotWrited = false;
        unsigned char data = 0;
        for (int i=0; i<8; i++) {
            if (mVoice[i].ecen) {
                data |= 1 << i;
            }
        }
        mDsp.WriteDsp(DSP_EON, data, false);
    }
}

void C700DSP::SetSrcn(int v, int value)
{
    // v chのsrcnからbrrdata,loopPointを設定する
    int brrPtr = mDirAddr + value * 4;
    int loopPtr = brrPtr + 2;
    int brrAddr = mRam[brrPtr+1] * 256 + mRam[brrPtr];
    int loopAddr = mRam[loopPtr+1] * 256 + mRam[loopPtr];
    
    setBrr( v, &mRam[brrAddr], loopAddr - brrAddr);
    if (v < 8) {
        mDsp.WriteDsp(DSP_SRCN + 0x10*v, static_cast<unsigned char>(value&0xff), false);
    }
}

void C700DSP::SetDir(int value)
{
    mDirAddr = (value & 0xff) << 8;
    mDsp.WriteDsp(DSP_DIR, static_cast<unsigned char>(value&0xff), false);
}

void C700DSP::setBrr(int v, unsigned char *brrdata, unsigned int loopPoint)
{
    mVoice[v].brrdata = brrdata;
    mVoice[v].loopPoint = loopPoint;
}

void C700DSP::WriteRam(int addr, const unsigned char *data, int size)
{
    // addrにdataをsizeバイト分転送する
    int startaddr = 0;
    int endaddr = addr + size;
    if (addr > startaddr) {
        startaddr = addr;
    }
    if (endaddr > 0x10000) {
        endaddr = 0x10000;
    }
    const unsigned char *srcPtr = data;
    for (int i=startaddr; i<endaddr; i++) {
        mRam[i] = *srcPtr;
        srcPtr++;
    }
    mDsp.WriteRam(addr, data, size);
    
    if (mBrrStartAddr > startaddr) {
        mBrrStartAddr = startaddr;
    }
    if (mBrrEndAddr < endaddr) {
        mBrrEndAddr = endaddr;
    }
}

void C700DSP::WriteRam(int addr, unsigned char data)
{
    mRam[addr & 0xffff] = data;
    mDsp.WriteRam(addr, data, false);
}

void C700DSP::Process1Sample(int &outl, int &outr)
{
    if (mUseRealEmulation) {
        mDsp.Process1Sample(outl, outr);
    }
    else if (!mDsp.IsHwAvailable()) {
        int		outx;
        
        for ( int v=0; v<kMaximumVoices; v++ ) {
            //outx = 0;
            //--
            {
                switch( mVoice[v].envstate ) {
                    case ATTACK:
                        if ( mVoice[v].ar == 15 ) {
                            mVoice[v].envx += 0x400;
                        }
                        else {
                            mVoice[v].envcnt -= ENVCNT[ ( mVoice[v].ar << 1 ) + 1 ];
                            if ( mVoice[v].envcnt > 0 ) {
                                break;
                            }
                            mVoice[v].envx += 0x20;       /* 0x020 / 0x800 = 1/64         */
                            mVoice[v].envcnt = CNT_INIT;
                        }
                        
                        if ( mVoice[v].envx > 0x7FF ) {
                            mVoice[v].envx = 0x7FF;
                            mVoice[v].envstate = DECAY;
                        }
                        break;
                        
                    case DECAY:
                        mVoice[v].envcnt -= ENVCNT[ mVoice[v].dr*2 + 0x10 ];
                        if ( mVoice[v].envcnt <= 0 ) {
                            mVoice[v].envcnt = CNT_INIT;
                            mVoice[v].envx -= ( ( mVoice[v].envx - 1 ) >> 8 ) + 1;
                        }
                        
                        if ( mVoice[v].envx <= 0x100 * ( mVoice[v].sl + 1 ) ) {
                            mVoice[v].envstate = SUSTAIN;
                        }
                        break;
                        
                    case SUSTAIN:
                        mVoice[v].envcnt -= ENVCNT[ mVoice[v].sr ];
                        if ( mVoice[v].envcnt > 0 ) {
                            break;
                        }
                        mVoice[v].envcnt = CNT_INIT;
                        mVoice[v].envx -= ( ( mVoice[v].envx - 1 ) >> 8 ) + 1;
                        break;
                        
                    case RELEASE:
                        mVoice[v].envx -= 0x8;
                        if ( mVoice[v].envx <= 0 ) {
                            mVoice[v].envx = -1;
                        }
                        break;
                }
            }
            
            if ( mVoice[v].envx < 0 ) {
                //outx = 0;
                continue;
            }
            
            for( ; mVoice[v].mixfrac >= 0; mVoice[v].mixfrac -= 4096 ) {
                if( !mVoice[v].headerCnt ) {	//ブロックの始まり
                    if( mVoice[v].end & 1 ) {	//データ終了フラグあり
                        if( mVoice[v].loop ) {
                            mVoice[v].memPtr = mVoice[v].loopPoint;	//読み出し位置をループポイントまで戻す
                        }
                        else {	//ループなし
                            mVoice[v].envx = 0;
                            while( mVoice[v].mixfrac >= 0 ) {
                                mVoice[v].sampbuf[mVoice[v].sampptr] = 0;
                                //outx = 0;
                                mVoice[v].sampptr  = ( mVoice[v].sampptr + 1 ) & 3;
                                mVoice[v].mixfrac -= 4096;
                            }
                            break;
                        }
                    }
                    
                    //開始バイトの情報を取得
                    mVoice[v].headerCnt = 8;
                    int headbyte = ( unsigned char )mVoice[v].brrdata[mVoice[v].memPtr++];
                    mVoice[v].range = headbyte >> 4;
                    mVoice[v].end = headbyte & 1;
                    mVoice[v].loop = headbyte & 2;
                    mVoice[v].filter = ( headbyte & 12 ) >> 2;
                }
                
                if ( mVoice[v].half == 0 ) {
                    mVoice[v].half = 1;
                    outx = ( ( signed char )mVoice[v].brrdata[ mVoice[v].memPtr ] ) >> 4;
                }
                else {
                    mVoice[v].half = 0;
                    outx = ( signed char )( mVoice[v].brrdata[ mVoice[v].memPtr++ ] << 4 );
                    outx >>= 4;
                    mVoice[v].headerCnt--;
                }
                //outx:4bitデータ
                
                if ( mVoice[v].range <= 0xC ) {
                    outx = ( outx << mVoice[v].range ) >> 1;
                }
                else {
                    outx &= ~0x7FF;
                }
                //outx:4bitデータ*Range
                
                switch( mVoice[v].filter ) {
                    case 0:
                        break;
                        
                    case 1:
                        outx += filter1(mVoice[v].smp1);
                        break;
                        
                    case 2:
                        outx += filter2(mVoice[v].smp1,mVoice[v].smp2);
                        break;
                        
                    case 3:
                        outx += filter3(mVoice[v].smp1,mVoice[v].smp2);
                        break;
                }
                
                // フィルタ後にクリップ
                if ( outx < -32768 ) {
                    outx = -32768;
                }
                else if ( outx > 32767 ) {
                    outx = 32767;
                }
                // y[-1]へ送る際に２倍されたらクランプ
                if (mNewADPCM) {
                    mVoice[v].smp2 = mVoice[v].smp1;
                    mVoice[v].smp1 = ( signed short )( outx << 1 );
                }
                else {
                    // 古いエミュレータの一部にはクランプを行わないものもある
                    // 音は実機と異なる
                    mVoice[v].smp2 = mVoice[v].smp1;
                    mVoice[v].smp1 = outx << 1;
                }
                mVoice[v].sampbuf[mVoice[v].sampptr] = mVoice[v].smp1;
                mVoice[v].sampptr = ( mVoice[v].sampptr + 1 ) & 3;
            }
            
            int fracPos = mVoice[v].mixfrac >> 4;
            int smpl = ( ( G4[ -fracPos - 1 ] * mVoice[v].sampbuf[ mVoice[v].sampptr ] ) >> 11 ) & ~1;
            smpl += ( ( G3[ -fracPos ]
                       * mVoice[v].sampbuf[ ( mVoice[v].sampptr + 1 ) & 3 ] ) >> 11 ) & ~1;
            smpl += ( ( G2[ fracPos ]
                       * mVoice[v].sampbuf[ ( mVoice[v].sampptr + 2 ) & 3 ] ) >> 11 ) & ~1;
            // openspcではなぜかここでもクランプさせていた
            // ここも無いと実機と違ってしまう
            if (mNewADPCM) {
                smpl = ( signed short )smpl;
            }
            smpl += ( ( G1[ fracPos ]
                       * mVoice[v].sampbuf[ ( mVoice[v].sampptr + 3 ) & 3 ] ) >> 11 ) & ~1;
            
            // ガウス補間後にクリップ
            if ( smpl > 32767 ) {
                smpl = 32767;
            }
            else if ( smpl < -32768 ) {
                smpl = -32768;
            }
            outx = smpl;
            
            mVoice[v].mixfrac += mVoice[v].pitch;
            
            outx = ( ( outx * mVoice[v].envx ) >> 11 ) & ~1;
            
            // ゲイン値の反映
            int vl = ( mVoice[v].vol_l * outx ) >> 7;
            int vr = ( mVoice[v].vol_r * outx ) >> 7;
            
            // エコー処理
            if ( mVoice[v].ecen ) {
                mEcho[0].Input(vl);
                mEcho[1].Input(vr);
            }
            //メインボリュームの反映
            outl += ( vl * mMainVolume_L ) >> 7;
            outr += ( vr * mMainVolume_R ) >> 7;
        }
        outl += mEcho[0].GetFxOut();
        outr += mEcho[1].GetFxOut();
    }
    else {
        outl = 0;
        outr = 0;
		mDsp.IncSampleInFrame();
    }
}

void C700DSP::BeginFrameProcess(double frameTime)
{
    mDsp.BeginFrameProcess(frameTime);
}

void C700DSP::onDeviceReady(void *ref)
{
    C700DSP   *This = reinterpret_cast<C700DSP*>(ref);
    // RAMを転送
    int writeBytes = This->mBrrEndAddr - This->mBrrStartAddr;
    if (writeBytes > 0) {
        // DIR領域を転送
        This->mDsp.WriteRam(0x200, &This->mRam[0x200], 0x400);
        // 波形領域を転送
        This->mDsp.WriteRam(This->mBrrStartAddr, &This->mRam[This->mBrrStartAddr], writeBytes);
    }
}

void C700DSP::onDeviceStop(void *ref)
{
    C700DSP   *This = reinterpret_cast<C700DSP*>(ref);
    
    // もしハード側にだけ書き込まれていたような場合のためにDIR領域を転送
    This->mDsp.WriteRam(0x200, &This->mRam[0x200], 0x400);
}
