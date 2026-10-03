/*
  ==============================================================================
    FreeTune v3.2 - Professional Auto-Tune Plugin
    DSP core: Thomas Baran autotalent + YIN improvements + Classic Mode
  ==============================================================================
*/
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const char* FreeTuneAudioProcessor::NOTE_PARAM_IDS[12] = {
    "noteA","noteBb","noteB","noteC","noteDb","noteD",
    "noteEb","noteE","noteF","noteGb","noteG","noteAb"
};

//==============================================================================
// FFT wrappers - backed by juce::dsp::FFT (SIMD-accelerated, from the juce_dsp
// module). The detector calls fft_forward / fft_inverse exactly as before; only
// the engine underneath changed. fft_data is a 2*nfft interleaved-complex
// workspace, which is the buffer layout JUCE's real-only transforms require.
fft_vars* fft_con(int nfft) {
    fft_vars* v = (fft_vars*)malloc(sizeof(fft_vars));
    v->nfft = nfft;
    v->numfreqs = nfft / 2 + 1;
    int order = 0; while ((1 << order) < nfft) ++order;   // exact log2 for power-of-two nfft
    v->fft = new juce::dsp::FFT(order);
    v->fft_data = (float*)calloc((size_t)nfft * 2, sizeof(float));
    return v;
}
void fft_des(fft_vars* v) {
    if (v) { delete v->fft; free(v->fft_data); free(v); }
}
void fft_forward(fft_vars* v, float* input, float* output_re, float* output_im) {
    const int nfft = v->nfft, hnfft = nfft / 2;
    for (int i = 0; i < nfft; i++) v->fft_data[i] = input[i];
    // Real-only forward: fills bins 0..N/2 as interleaved {re,im,re,im,...}.
    v->fft->performRealOnlyForwardTransform(v->fft_data, true);
    for (int i = 0; i <= hnfft; i++) {
        output_re[i] = v->fft_data[2 * i];
        output_im[i] = v->fft_data[2 * i + 1];
    }
}
void fft_inverse(fft_vars* v, float* input_re, float* input_im, float* output) {
    const int nfft = v->nfft, hnfft = nfft / 2;
    for (int i = 0; i <= hnfft; i++) {
        v->fft_data[2 * i] = input_re[i];
        v->fft_data[2 * i + 1] = input_im[i];
    }
    v->fft->performRealOnlyInverseTransform(v->fft_data);
    for (int i = 0; i < nfft; i++) output[i] = v->fft_data[i];
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout FreeTuneAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("tune", 1), "Concert A", 430.f, 450.f, 440.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("amount", 1), "Amount", 0.f, 1.f, 1.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("speed", 1), "Retune Speed", 0.f, 100.f, 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("flex", 1), "Flex-Tune", 0.f, 1.f, 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("humanize", 1), "Humanize", 0.f, 100.f, 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("vibrato", 1), "Natural Vibrato", 0.f, 1.f, 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("shift", 1), "Pitch Shift", -12.f, 12.f, 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("fcorr", 1), "Formant Correct", 0.f, 1.f, 1.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("fwarp", 1), "Formant Warp", -1.f, 1.f, 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("mix", 1), "Mix", 0.f, 1.f, 1.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("classic", 1), "Classic Mode", 0.f, 1.f, 0.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("range", 1), "Vocal Range", 0.f, 4.f, 2.f));
    // Key + Scale selectors. The note toggles drive the DSP; these remember which key and
    // scale are selected (saved with the project) and let the editor apply all 24 keys.
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("key", 1), "Key",
        juce::StringArray{ "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID("scaleType", 1), "Scale",
        juce::StringArray{ "Major", "Minor", "Chromatic", "Custom" }, 2));
    const char* noteNames[12] = { "A","Bb","B","C","Db","D","Eb","E","F","Gb","G","Ab" };
    for (int i = 0;i < 12;i++)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID(NOTE_PARAM_IDS[i], 1), noteNames[i], 0.f, 1.f, 1.f));
    return layout;
}

//==============================================================================
FreeTuneAudioProcessor::FreeTuneAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
    ),
#else
    :
#endif
apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    pTune = apvts.getRawParameterValue("tune");
    pAmount = apvts.getRawParameterValue("amount");
    pSpeed = apvts.getRawParameterValue("speed");
    pFlex = apvts.getRawParameterValue("flex");
    pHumanize = apvts.getRawParameterValue("humanize");
    pVibrato = apvts.getRawParameterValue("vibrato");
    pShift = apvts.getRawParameterValue("shift");
    pFcorr = apvts.getRawParameterValue("fcorr");
    pFwarp = apvts.getRawParameterValue("fwarp");
    pMix = apvts.getRawParameterValue("mix");
    pClassic = apvts.getRawParameterValue("classic");
    pRange = apvts.getRawParameterValue("range");
    for (int i = 0;i < 12;i++) pNotes[i] = apvts.getRawParameterValue(NOTE_PARAM_IDS[i]);
    memset(mVibratoHistory, 0, sizeof(mVibratoHistory));
}

FreeTuneAudioProcessor::~FreeTuneAudioProcessor() { resetDSP(); }

//==============================================================================
void FreeTuneAudioProcessor::resetDSP()
{
    if (!dspInitialised) return;
    fft_des(mfmembvars); mfmembvars = nullptr;
    free(mcbi);free(mcbf);free(mcbo);free(mcbwindow);
    free(mhannwindow);free(macwinv);free(mfrag);
    free(mffttime);free(mfftfreqre);free(mfftfreqim);
    free(myindiff);free(myincumE);
    free(mfk);free(mfb);free(mfc);free(mfrb);free(mfrc);
    free(mfsig);free(mfsmooth);free(mftvec);
    mcbi = mcbf = mcbo = mcbwindow = mhannwindow = macwinv = mfrag = nullptr;
    mffttime = mfftfreqre = mfftfreqim = nullptr;
    myindiff = myincumE = nullptr;
    mfk = mfb = mfc = mfrb = mfrc = mfsig = mfsmooth = mftvec = nullptr;
    if (mfbuff) { for (int i = 0;i < mford;i++) free(mfbuff[i]); free(mfbuff); mfbuff = nullptr; }
    dspInitialised = false;
}

void FreeTuneAudioProcessor::initDSP(unsigned long sr)
{
    resetDSP();
    mfs = sr;
    mcbsize = (sr >= 88200) ? 4096 : 2048;
    mcorrsize = mcbsize / 2 + 1;

    // Default pitch bounds — overridden per-block by vocal range param
    mpmax = 1.0f / 70.0f;
    mpmin = 1.0f / 1000.0f;
    mnmax = (unsigned long)(sr * mpmax);
    if (mnmax > mcorrsize) mnmax = mcorrsize;
    mnmin = (unsigned long)(sr * mpmin);

    mcbi = (float*)calloc(mcbsize, sizeof(float));
    mcbf = (float*)calloc(mcbsize, sizeof(float));
    mcbo = (float*)calloc(mcbsize, sizeof(float));
    mcbiwr = 0; mcbord = 0; mlfophase = 0;

    mford = 7;
    // Formant analysis adaptation speed. Original autotalent used 80 (~2 ms), which
    // tracks changes *within* each vocal cycle and turns into grit whenever the pitch
    // is shifted. 20 (~7 ms) follows the vowel envelope instead: measured as clean as
    // the input while still keeping ~93% of the vowel shape on a +5 semitone shift.
    mfalph = powf(0.001f, 20.0f / (float)sr);
    mflamb = -(0.8517f * sqrtf(atanf(0.06583f * (float)sr)) - 0.1916f);
    mfk = (float*)calloc(mford, sizeof(float));
    mfb = (float*)calloc(mford, sizeof(float));
    mfc = (float*)calloc(mford, sizeof(float));
    mfrb = (float*)calloc(mford, sizeof(float));
    mfrc = (float*)calloc(mford, sizeof(float));
    mfsig = (float*)calloc(mford, sizeof(float));
    mfsmooth = (float*)calloc(mford, sizeof(float));
    mfhp = 0; mflp = 0;
    mflpa = powf(0.001f, 10.0f / (float)sr);
    mfbuff = (float**)malloc(mford * sizeof(float*));
    for (int i = 0;i < mford;i++) mfbuff[i] = (float*)calloc(mcbsize, sizeof(float));
    mftvec = (float*)calloc(mford, sizeof(float));
    mfmute = 1; mfmutealph = powf(0.001f, 1.0f / (float)sr);

    mhannwindow = (float*)calloc(mcbsize, sizeof(float));
    for (unsigned long i = 0;i < mcbsize;i++)
        mhannwindow[i] = -0.5f * cosf(2.0f * (float)M_PI * (float)i / (float)mcbsize) + 0.5f;

    mcbwindow = (float*)calloc(mcbsize, sizeof(float));
    for (unsigned long i = 0;i < mcbsize / 2;i++)
        mcbwindow[i + mcbsize / 4] = -0.5f * cosf(4.0f * (float)M_PI * (float)i / ((float)mcbsize - 1)) + 0.5f;

    // IMPROVEMENT: Run pitch detection 16x per buffer instead of 4x
    // Original autotalent used noverlap=4, we use 16 for faster tracking
    mnoverlap = 16;

    mfmembvars = fft_con((int)mcbsize);
    mffttime = (float*)calloc(mcbsize, sizeof(float));
    mfftfreqre = (float*)calloc(mcorrsize, sizeof(float));
    mfftfreqim = (float*)calloc(mcorrsize, sizeof(float));

    macwinv = (float*)calloc(mcbsize, sizeof(float));
    myindiff = (float*)calloc(mcorrsize, sizeof(float));
    myincumE = (float*)calloc(mcorrsize, sizeof(float));
    for (unsigned long i = 0;i < mcbsize;i++) mffttime[i] = mcbwindow[i];
    fft_forward(mfmembvars, mcbwindow, mfftfreqre, mfftfreqim);
    for (unsigned long i = 0;i < mcorrsize;i++) {
        mfftfreqre[i] = mfftfreqre[i] * mfftfreqre[i] + mfftfreqim[i] * mfftfreqim[i];
        mfftfreqim[i] = 0;
    }
    fft_inverse(mfmembvars, mfftfreqre, mfftfreqim, mffttime);
    for (unsigned long i = 1;i < mcbsize;i++) {
        macwinv[i] = mffttime[i] / mffttime[0];
        macwinv[i] = (macwinv[i] > 0.000001f) ? 1.0f / macwinv[i] : 0.0f;
    }
    macwinv[0] = 1;

    mphprdd = 0.01f;
    minphinc = 1.0 / (mphprdd * (double)sr);
    mphincfact = 1; mphasein = 0; mphaseout = 0; mfragFrac = 0;
    mfrag = (float*)calloc(mcbsize, sizeof(float));
    mfragsize = 0;

    maref = 440; minpitch = 0; mconf = 0; moutpitch = 0;

    // Minimum YIN clarity for a pitch reading to be trusted. Kept just above the
    // gate's "unvoiced" level (0.40) so noisy frames can't drag the target around.
    mvthresh = 0.45f;

    mHumanizeTimer = 0; mLastTargetPitch = 0;
    mFlexSmoothPitch = 0; mClassicShift = 1;
    mTargetCorr = 0; mCurrentCorr = 0; mGlideCoeff = 1; mShiftSemis = 0;
    mCurNote = 0; mHasNote = false;
    mPitchHist[0] = mPitchHist[1] = mPitchHist[2] = 0; mPitchHistCount = 0;
    mVoicedTarget = 0; mVoicedGain = 0;
    mGateAtkCoeff = 1.0f - expf(-1.0f / (0.003f * (float)sr));   // ~3 ms attack (catch onsets)
    mGateRelCoeff = 1.0f - expf(-1.0f / (0.030f * (float)sr));   // ~30 ms release (smooth tails)
    memset(mVibratoHistory, 0, sizeof(mVibratoHistory));
    mVibratoIndex = 0; mVibratoDepth = 0;

    setLatencySamples((int)mcbsize - 1);
    dspInitialised = true;
}

//==============================================================================
void FreeTuneAudioProcessor::prepareToPlay(double sampleRate, int) { initDSP((unsigned long)sampleRate); }
void FreeTuneAudioProcessor::releaseResources() { resetDSP(); }

const juce::String FreeTuneAudioProcessor::getName() const { return JucePlugin_Name; }
bool FreeTuneAudioProcessor::acceptsMidi()  const { return false; }
bool FreeTuneAudioProcessor::producesMidi() const { return false; }
bool FreeTuneAudioProcessor::isMidiEffect() const { return false; }
double FreeTuneAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int  FreeTuneAudioProcessor::getNumPrograms() { return 1; }
int  FreeTuneAudioProcessor::getCurrentProgram() { return 0; }
void FreeTuneAudioProcessor::setCurrentProgram(int) {}
const juce::String FreeTuneAudioProcessor::getProgramName(int) { return {}; }
void FreeTuneAudioProcessor::changeProgramName(int, const juce::String&) {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool FreeTuneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return true;
}
#endif

//==============================================================================
void FreeTuneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (!dspInitialised) return;

    auto totalIn = getTotalNumInputChannels();
    auto totalOut = getTotalNumOutputChannels();
    for (auto i = totalIn;i < totalOut;++i) buffer.clear(i, 0, buffer.getNumSamples());

    // Read all parameters once per block (not per sample — saves CPU)
    float fAmount = pAmount->load();
    float fSpeed = pSpeed->load();
    float fFlex = pFlex->load();
    float fHumanize = pHumanize->load();
    float fVibrato = pVibrato->load();
    float fShift = pShift->load();
    int   iFcorr = (pFcorr->load() > 0.5f) ? 1 : 0;
    float fFwarp = pFwarp->load();
    float fMix = pMix->load();
    bool  bClassic = (pClassic->load() > 0.5f);
    int   ri = juce::jlimit(0, 4, (int)pRange->load());
    maref = pTune->load();

    // Retune-speed -> per-sample glide coefficient.
    // Speed 0 = instant (hard tune); Speed 100 = ~200 ms portamento. The squared
    // curve puts the useful fast range under more of the knob travel.
    {
        float gms = fSpeed / 100.0f;
        gms = gms * gms * 200.0f;          // 0..200 ms
        if (bClassic) gms = 0.0f;          // classic mode = instant snap
        // Floor of ~3 ms (about one detection frame). Still sounds instant, but turns
        // the per-frame correction steps into tiny ramps, removing zipper/crackle.
        if (gms < 3.0f) gms = 3.0f;
        float gtau = gms * (float)mfs / 1000.0f;
        mGlideCoeff = (gtau > 1.0f) ? (1.0f - expf(-1.0f / gtau)) : 1.0f;
    }

    // Vocal range → frequency bounds for pitch detector
    // Soprano: 200-1100Hz, Alto: 150-900Hz, Tenor: 100-700Hz,
    // Baritone: 80-600Hz, Bass: 60-500Hz
    const float rangeMaxHz[5] = { 1100.f,900.f,700.f,600.f,500.f };
    const float rangeMinHz[5] = { 200.f,150.f,100.f, 80.f, 60.f };
    mnmax = juce::jlimit((unsigned long)1, mcorrsize, (unsigned long)((float)mfs / rangeMinHz[ri]));
    mnmin = juce::jlimit((unsigned long)1, mnmax - 1, (unsigned long)((float)mfs / rangeMaxHz[ri]));

    // Note/scale setup
    int iNotes[12];
    for (int i = 0;i < 12;i++) iNotes[i] = (pNotes[i]->load() > 0.5f) ? 1 : -1;
    int numNotes = 0;
    for (int i = 0;i < 12;i++) if (iNotes[i] > 0) numNotes++;
    if (numNotes == 0) {                      // no notes selected -> treat as chromatic
        for (int i = 0;i < 12;i++) iNotes[i] = 1;
        numNotes = 12;
    }

    // Precompute formant warp coefficients
    float falph = mfalph, foma = 1.0f - falph, flpa = mflpa, flamb = mflamb;
    float tfw = powf(2.0f, fFwarp / 2.0f) * (1.0f + flamb) / (1.0f - flamb);
    float frlamb = (tfw - 1.0f) / (tfw + 1.0f);

    long int N = (long int)mcbsize;
    long int Nf = (long int)mcorrsize;
    long int fs = (long int)mfs;
    float aref = maref;

    // Local copies of DSP state for this block
    float pperiod = mpmax;
    float inpitch = minpitch;
    float conf = mconf;
    float outpitch = moutpitch;

    float* pfInput = buffer.getWritePointer(0);
    float* pfOutputL = buffer.getWritePointer(0);
    float* pfOutputR = (totalOut > 1) ? buffer.getWritePointer(1) : buffer.getWritePointer(0);
    int    SampleCount = buffer.getNumSamples();

    for (int lSampleIndex = 0; lSampleIndex < SampleCount; lSampleIndex++)
    {
        float tf = pfInput[lSampleIndex];
        long int ti4 = (long int)mcbiwr;
        mcbi[ti4] = tf;

        // ── Formant pre-filter (Levinson-Durbin style adaptive LPC) ─────────
        if (iFcorr >= 1) {
            float fa = tf - mfhp; mfhp = tf;
            float fb = fa;
            for (int ti = 0;ti < mford;ti++) {
                mfsig[ti] = fa * fa * foma + mfsig[ti] * falph;
                float fc = (fb - mfc[ti]) * flamb + mfb[ti];
                mfc[ti] = fc; mfb[ti] = fb;
                float fk = fa * fc * foma + mfk[ti] * falph;
                mfk[ti] = fk;
                tf = fk / (mfsig[ti] + 0.000001f);
                tf = tf * foma + mfsmooth[ti] * falph;
                mfsmooth[ti] = tf;
                mfbuff[ti][ti4] = tf;
                fb = fc - tf * fa; fa = fa - tf * fc;
            }
            mcbf[ti4] = fa;
        }
        else {
            mcbf[ti4] = tf;
        }

        mcbiwr++;
        if (mcbiwr >= (unsigned long)N) mcbiwr = 0;

        // ── Pitch detection — runs every N/mnoverlap samples ─────────────────
        if (mcbiwr % ((unsigned long)N / (unsigned long)mnoverlap) == 0)
        {
            // ===== YIN pitch detection =====
            // Integration window W is zero-padded inside the length-N FFT so the
            // FFT autocorrelation equals the true (linear) autocorrelation for the
            // lags we search. W = N/2 gives clean lags up to N/2-1.
            const long int W = N / 2;
            long int tauMax = (long int)mnmax;  if (tauMax > W - 1) tauMax = W - 1;
            long int tauMin = (long int)mnmin;  if (tauMin < 2)     tauMin = 2;

            // 1) Analysis window into the FFT time buffer (chronological), zero-pad rest.
            //    Offset N/8 samples into the past so the pitch estimate lines up in time with
            //    the grains the PSOLA shifter is processing. Measured optimum: at 44.1 kHz it
            //    cut leftover wobble on hard-tuned vibrato from 15% to 7% and raised the
            //    harmonic-to-noise ratio by ~5.5 dB versus no offset.
            long int wr = (long int)mcbiwr - N / 8;
            for (long int i = 0; i < W; i++)
                mffttime[i] = mcbi[(wr - W + i + 2 * N) % N];
            for (long int i = W; i < N; i++)
                mffttime[i] = 0.0f;

            // 2) Cumulative energy prefix sums:  myincumE[k] = sum_{j<k} x[j]^2
            myincumE[0] = 0.0f;
            for (long int i = 0; i < W; i++)
                myincumE[i + 1] = myincumE[i] + mffttime[i] * mffttime[i];
            float totalE = myincumE[W];

            // 3) Autocorrelation via FFT (power spectrum -> inverse FFT)
            fft_forward(mfmembvars, mffttime, mfftfreqre, mfftfreqim);
            for (long int i = 0; i < Nf; i++) {
                mfftfreqre[i] = mfftfreqre[i] * mfftfreqre[i] + mfftfreqim[i] * mfftfreqim[i];
                mfftfreqim[i] = 0.0f;
            }
            fft_inverse(mfmembvars, mfftfreqre, mfftfreqim, mffttime); // mffttime[tau] = scaled ACF

            // Recover the FFT round-trip scale from lag 0 (ACF[0] must equal totalE).
            // Kept deliberately after the JUCE swap: one divide per frame makes the
            // detector correct for ANY FFT normalization convention, so the ACF can be
            // safely combined with the (true-scale) energy terms. With juce::dsp::FFT
            // this factor is ~1.0 - it's now insurance, not a fix.
            float acfScale = (mffttime[0] > 0.0f) ? (totalE / mffttime[0]) : 0.0f;

            // 4) Difference function + cumulative-mean normalization (CMNDF)
            //    d(tau)  = Ehead + Etail - 2*ACF(tau)
            //    d'(tau) = d(tau) / ((1/tau) * sum_{k=1..tau} d(k))
            float runningSum = 0.0f;
            myindiff[0] = 1.0f;
            for (long int tau = 1; tau <= tauMax; tau++) {
                float Ehead = myincumE[W - tau];
                float Etail = totalE - myincumE[tau];
                float d = Ehead + Etail - 2.0f * mffttime[tau] * acfScale;
                if (d < 0.0f) d = 0.0f;               // clamp tiny FP negatives
                runningSum += d;
                myindiff[tau] = (runningSum > 0.0f) ? d * (float)tau / runningSum : 1.0f;
            }

            // 5) Absolute-threshold valley pick: first valley below threshold wins.
            //    This is what kills octave errors - raw peak-picking jumps to harmonics.
            const float yinThresh = 0.12f;   // 0.10 = stricter/fewer octave errors, 0.15 = catches more
            long int tauStar = -1;
            for (long int tau = tauMin; tau <= tauMax; tau++) {
                if (myindiff[tau] < yinThresh) {
                    while (tau + 1 <= tauMax && myindiff[tau + 1] < myindiff[tau]) tau++; // descend to bottom
                    tauStar = tau;
                    break;
                }
            }
            if (tauStar < 0) {  // nothing below threshold -> likely unvoiced; take global min
                float best = 1.0e9f;
                for (long int tau = tauMin; tau <= tauMax; tau++)
                    if (myindiff[tau] < best) { best = myindiff[tau]; tauStar = tau; }
            }
            if (tauStar < tauMin) tauStar = tauMin;   // safety: never leave tauStar invalid

            // 6) Parabolic interpolation on the CMNDF for sub-sample period accuracy
            float betterTau = (float)tauStar;
            if (tauStar > tauMin && tauStar < tauMax) {
                float s0 = myindiff[tauStar - 1], s1 = myindiff[tauStar], s2 = myindiff[tauStar + 1];
                float pdenom = s0 + s2 - 2.0f * s1;
                if (fabsf(pdenom) > 1.0e-9f)
                    betterTau = (float)tauStar + 0.5f * (s0 - s2) / pdenom;
            }

            pperiod = betterTau / (float)fs;
            conf = 1.0f - myindiff[tauStar];     // "clarity": 1 = perfectly periodic, 0 = noise
            if (conf < 0.0f) conf = 0.0f;
            if (conf > 1.0f) conf = 1.0f;

            // Convert period to semitones (relative to concert A)
            tf = -12.0f * log10f(aref * pperiod) * L2SC;

            // IMPROVEMENT: Classic mode ignores confidence threshold entirely —
            // it tries to tune every single frame, creating artifacts on
            // unvoiced sounds (breaths, consonants) that ARE the T-Pain sound
            if (bClassic) {
                inpitch = tf; minpitch = tf;
            }
            else if (conf >= mvthresh) {
                inpitch = tf; minpitch = tf;
            }
            mconf = conf;

            // Voiced/unvoiced gate decision (hysteresis). Classic tunes everything,
            // so the gate is bypassed there and we force "always voiced".
            if (bClassic) {
                mVoicedTarget = 1.0f;
            }
            else if (mVoicedTarget > 0.5f) {       // voiced -> need a clear drop to leave
                if (conf < 0.40f) mVoicedTarget = 0.0f;
            }
            else {                                  // unvoiced -> need clear voicing to enter
                if (conf > 0.55f) mVoicedTarget = 1.0f;
            }

            // ── 3-frame median filter on the detected pitch ────────────────────
            // One bad frame (octave error at a consonant onset, etc.) can't move the
            // target anymore; it takes two agreeing frames.
            mPitchHist[2] = mPitchHist[1];
            mPitchHist[1] = mPitchHist[0];
            mPitchHist[0] = inpitch;
            if (mPitchHistCount < 3) mPitchHistCount++;
            float p = inpitch;
            if (mPitchHistCount >= 3) {
                float a = mPitchHist[0], b = mPitchHist[1], cc = mPitchHist[2];
                p = juce::jmax(juce::jmin(a, b), juce::jmin(juce::jmax(a, b), cc));
            }

            // Update pitch display for UI
            detectedPitchHz.store(aref * powf(2.0f, p / 12.0f));
            detectedPitchConf.store(conf);

            // ── Vibrato detection (drives the Vibrato knob) ──────────────────
            mVibratoHistory[mVibratoIndex % VIBRATO_HISTORY] = p;
            mVibratoIndex++;
            float vMean = 0;
            for (int vi = 0;vi < VIBRATO_HISTORY;vi++) vMean += mVibratoHistory[vi];
            vMean /= (float)VIBRATO_HISTORY;
            float vVar = 0;
            for (int vi = 0;vi < VIBRATO_HISTORY;vi++) {
                float d = mVibratoHistory[vi] - vMean; vVar += d * d;
            }
            mVibratoDepth = sqrtf(vVar / (float)VIBRATO_HISTORY);
            float vibratoReduction = 1.0f;
            if (fVibrato > 0.0f && mVibratoDepth > 0.3f)
                vibratoReduction = 1.0f - fVibrato * juce::jmin(1.0f, (mVibratoDepth - 0.3f) / 0.7f);

            // ── Target note selection with hysteresis ────────────────────────
            // Semitone grid relative to concert A; note class 0 = A, matching the
            // A, Bb, B, C... order of the note buttons.
            {
                auto isAllowed = [&](int n) { return iNotes[((n % 12) + 12) % 12] > 0; };
                int base = (int)floorf(p);
                int best = base;
                float bestDist = 1.0e9f;
                for (int n = base - 12; n <= base + 13; n++) {
                    if (!isAllowed(n)) continue;
                    float dd = fabsf(p - (float)n);
                    if (dd < bestDist) { bestDist = dd; best = n; }
                }
                // Classic switches notes eagerly; normal mode needs a clear commitment.
                const float hyst = bClassic ? 0.10f : 0.25f;
                if (!mHasNote || !isAllowed(mCurNote)) {
                    mCurNote = best; mHasNote = true;
                }
                else if (best != mCurNote) {
                    float dCur = fabsf(p - (float)mCurNote);
                    if (dCur - bestDist > hyst || dCur > 1.5f) {
                        mCurNote = best;
                        mHumanizeTimer = 0.0f;     // new note -> restart sustain timer
                    }
                }
            }

            // ── Correction amount ────────────────────────────────────────────
            float err = (float)mCurNote - p;               // semitones to reach the note
            float amt = fAmount * vibratoReduction;

            // Flex-Tune: full correction close to the note, fading out smoothly as the
            // singer moves away, so deliberate expressive bends are left alone.
            if (!bClassic && fFlex > 0.0f) {
                float zone = 0.5f * (1.0f - fFlex) + 0.05f;  // full-correction radius
                float d = fabsf(err);
                float w = (d <= zone) ? 1.0f : juce::jmax(0.0f, 1.0f - (d - zone) / 0.15f);
                amt *= w;
            }

            // Humanize: ease off correction on long sustained notes.
            if (!bClassic && fHumanize > 0.0f) {
                mHumanizeTimer += 1.0f;
                float sustainThresh = (float)mfs / (float)mcbsize * (float)mnoverlap * 0.3f;
                if (mHumanizeTimer > sustainThresh) {
                    float hamt = (mHumanizeTimer - sustainThresh) / (sustainThresh * 2.0f);
                    hamt = juce::jmin(1.0f, hamt) * (fHumanize / 100.0f) * 0.5f;
                    amt *= (1.0f - hamt);
                }
            }

            mTargetCorr = amt * err;

            // Live readings for the editor's tuning meter (read by the UI timer).
            meterInputCents.store(-err * 100.0f);                    // singer vs. target note
            meterOutputCents.store((-err + mCurrentCorr) * 100.0f);  // after correction
            meterTargetMidi.store(69 + mCurNote);                     // MIDI note being tuned to
            meterVoiced.store(mVoicedTarget > 0.5f);
            mShiftSemis = fShift;                          // pure transpose, any scale
            outpitch = p + mTargetCorr + mShiftSemis;
            moutpitch = outpitch;

            // Input phase rate for the PSOLA grain extractor, from the filtered pitch.
            minphinc = aref * pow(2.0, p / 12.0) / (double)fs;
        }

        // ── PSOLA Pitch Shifter ───────────────────────────────────────────────
        // Pitch-synchronous overlap-add: reads input at one rate, writes output
        // at a different rate determined by the pitch shift ratio.
        //
        // Per-sample retune glide (Auto-Tune style): slew the CORRECTION toward the
        // target correction. Speed 0 -> correction applied almost instantly (hard tune,
        // flat output). Slower speeds -> correction moves slowly, so the singer's own
        // vibrato and scoops pass through while the average pitch is pulled in tune.
        mCurrentCorr += (mTargetCorr - mCurrentCorr) * mGlideCoeff;
        {
            double totalShift = (double)mCurrentCorr + (double)mShiftSemis;
            if (totalShift > 24.0)  totalShift = 24.0;
            if (totalShift < -24.0) totalShift = -24.0;
            mphincfact = pow(2.0, totalShift / 12.0);
        }
        moutphinc = minphinc * mphincfact;

        mphasein += minphinc;
        mphaseout += moutphinc;

        // When input phase completes one cycle, grab a new fragment.
        // The phase accumulator crossed 1.0 part-way through the previous sample; record
        // that fractional offset so the grain is read from its exact (sub-sample) moment.
        if (mphasein >= 1.0) {
            mphasein -= 1.0;
            mfragFrac = (float)(mphasein / minphinc);      // ideal extraction was this long ago
            long int ti2b = (long int)mcbiwr - N / 2;
            for (long int ti = -N / 2;ti < N / 2;ti++)
                mfrag[(ti + N) % N] = mcbf[(ti + ti2b + N) % N];
        }

        // When output phase completes one cycle, overlap-add the shifted grain.
        // Grain = two output periods long (Hann), so consecutive grains spaced one period
        // apart always sum to constant gain. Placement and reading are both done at exact
        // fractional positions, which keeps overlapping grains phase-coherent (less grit).
        if (mphaseout >= 1.0) {
            mphaseout -= 1.0;
            const double outFrac = mphaseout / moutphinc;    // ideal placement was this long ago
            double Pout = 1.0 / moutphinc;                   // exact output period (samples)
            if (Pout > (double)(N / 2 - 4)) Pout = (double)(N / 2 - 4);
            const long int half = (long int)Pout + 1;
            const long int ti2b = (long int)mcbord + N / 2;
            const float invP = (float)(1.0 / Pout);
            for (long int ti = -half; ti <= half; ti++) {
                const float rel = (float)ti + (float)outFrac;          // offset from ideal centre
                if (fabsf(rel) >= (float)Pout) continue;
                const float hannVal = 0.5f + 0.5f * cosf((float)M_PI * rel * invP);
                // 4-point (cubic Lagrange) interpolation, centred correctly for negative
                // positions via floorf (plain (int) cast rounds toward zero there).
                const float indd = (float)mphincfact * rel - mfragFrac;
                const int ind1 = (int)floorf(indd), ind0 = ind1 - 1, ind2 = ind1 + 1, ind3 = ind1 + 2;
                const float val0 = mfrag[(ind0 + N) % N], val1 = mfrag[(ind1 + N) % N];
                const float val2 = mfrag[(ind2 + N) % N], val3 = mfrag[(ind3 + N) % N];
                float vald = 0;
                vald -= 0.166666666667f * val0 * (indd - (float)ind1) * (indd - (float)ind2) * (indd - (float)ind3);
                vald += 0.5f * val1 * (indd - (float)ind0) * (indd - (float)ind2) * (indd - (float)ind3);
                vald -= 0.5f * val2 * (indd - (float)ind0) * (indd - (float)ind1) * (indd - (float)ind3);
                vald += 0.166666666667f * val3 * (indd - (float)ind0) * (indd - (float)ind1) * (indd - (float)ind2);
                mcbo[(ti + ti2b + N) % N] += vald * hannVal;
            }
        }

        tf = mcbo[mcbord];
        mcbo[mcbord] = 0;
        mcbord++;
        if (mcbord >= (unsigned long)N) mcbord = 0;

        // ── Formant post-filter (re-applies vocal character after shifting) ────
        ti4 = (long int)(mcbiwr + 2) % N;
        if (iFcorr >= 1) {
            float tf2b = tf;
            // 0-response pass
            float fa = 0, fb = 0;
            for (int ti = 0;ti < mford;ti++) {
                float fc = (fb - mfrc[ti]) * frlamb + mfrb[ti];
                float ftf = mfbuff[ti][ti4];
                fb = fc - ftf * fa; mftvec[ti] = ftf * fc; fa -= mftvec[ti];
            }
            float f0resp = -fa;
            for (int ti = mford - 1;ti >= 0;ti--) f0resp += mftvec[ti];
            // 1-response pass
            fa = 1; fb = 1;
            for (int ti = 0;ti < mford;ti++) {
                float fc = (fb - mfrc[ti]) * frlamb + mfrb[ti];
                float ftf = mfbuff[ti][ti4];
                fb = fc - ftf * fa; mftvec[ti] = ftf * fc; fa -= mftvec[ti];
            }
            float f1resp = -fa;
            for (int ti = mford - 1;ti >= 0;ti--) f1resp += mftvec[ti];
            // Solve for output
            float denom2 = 1.0f - f1resp + f0resp;
            tf2b = (denom2 != 0.0f) ? (2.0f * tf2b + f0resp) / denom2 : 0.0f;
            // Update delay registers
            fa = tf2b; fb = tf2b;
            for (int ti = 0;ti < mford;ti++) {
                float fc = (fb - mfrc[ti]) * frlamb + mfrb[ti];
                mfrc[ti] = fc; mfrb[ti] = fb;
                float ftf = mfbuff[ti][ti4];
                fb = fc - ftf * fa; fa -= ftf * fc;
            }
            tf = tf2b;
            tf += flpa * mflp; mflp = tf;
            // Ramp up gain when formant correction first enabled
            if (mfmute > 0.5f) tf *= (mfmute - 0.5f) * 2.0f;
            else tf = 0.0f;
            mfmute = (1.0f - mfmutealph) + mfmutealph * mfmute;
        }
        else {
            mfmute = 0;
        }

        // Voiced/unvoiced gate: slew the gate gain per sample, then cross-fade the wet
        // (processed) signal back to the dry input on unvoiced frames. Fast attack so
        // onsets aren't missed; slower release so note tails aren't chopped.
        {
            float gcoef = (mVoicedTarget > mVoicedGain) ? mGateAtkCoeff : mGateRelCoeff;
            mVoicedGain += (mVoicedTarget - mVoicedGain) * gcoef;
        }
        float dryIn = mcbi[ti4];
        float gated = mVoicedGain * tf + (1.0f - mVoicedGain) * dryIn;

        // Write final output — blend dry and wet by Mix knob
        float outSample = fMix * gated + (1.0f - fMix) * dryIn;
        pfOutputL[lSampleIndex] = outSample;
        if (totalOut > 1) pfOutputR[lSampleIndex] = outSample;
    }
}

//==============================================================================
bool FreeTuneAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* FreeTuneAudioProcessor::createEditor()
{
    return new FreeTuneAudioProcessorEditor(*this);
}

void FreeTuneAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}
void FreeTuneAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FreeTuneAudioProcessor();
}