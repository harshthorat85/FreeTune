/*
  ==============================================================================
    FreeTune v3.0 - Professional Auto-Tune Plugin
    DSP core: Thomas Baran autotalent + modern enhancements
  ==============================================================================
*/
#pragma once
#include <JuceHeader.h>

#define L2SC (float)3.32192809488736218171
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct fft_vars {
    int nfft;
    int numfreqs;
    juce::dsp::FFT* fft;   // SIMD-accelerated FFT engine (requires the juce_dsp module)
    float* fft_data;       // interleaved-complex workspace, length 2*nfft
};

fft_vars* fft_con(int nfft);
void fft_des(fft_vars* membvars);
void fft_forward(fft_vars* membvars, float* input, float* output_re, float* output_im);
void fft_inverse(fft_vars* membvars, float* input_re, float* input_im, float* output);

//==============================================================================
class FreeTuneAudioProcessor : public juce::AudioProcessor
{
public:
    FreeTuneAudioProcessor();
    ~FreeTuneAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Public for editor to read detected pitch display
    std::atomic<float> detectedPitchHz{ 0.0f };
    std::atomic<float> detectedPitchConf{ 0.0f };

    // Tuning meter feed (written by the audio thread, read by the editor)
    std::atomic<float> meterInputCents{ 0.0f };   // detected pitch, cents from the target note
    std::atomic<float> meterOutputCents{ 0.0f };  // corrected pitch, cents from the target note
    std::atomic<int>   meterTargetMidi{ -1 };     // target note as a MIDI number (-1 = none yet)
    std::atomic<bool>  meterVoiced{ false };      // true while a sung (voiced) note is present

private:
    void initDSP(unsigned long sampleRate);
    void resetDSP();
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Parameters ──────────────────────────────────────────────────────────
    std::atomic<float>* pTune = nullptr; // Concert A ref
    std::atomic<float>* pAmount = nullptr; // Correction amount
    std::atomic<float>* pSpeed = nullptr; // Retune speed (0=instant, 1=slow)
    std::atomic<float>* pFlex = nullptr; // Flex-Tune threshold
    std::atomic<float>* pHumanize = nullptr; // Humanize (reduce correction on sustained notes)
    std::atomic<float>* pVibrato = nullptr; // Natural vibrato preserve
    std::atomic<float>* pShift = nullptr; // Pitch shift semitones
    std::atomic<float>* pFcorr = nullptr; // Formant correction
    std::atomic<float>* pFwarp = nullptr; // Formant warp
    std::atomic<float>* pMix = nullptr; // Dry/wet mix
    std::atomic<float>* pClassic = nullptr; // Classic mode (hard snap)
    std::atomic<float>* pRange = nullptr; // Vocal range (0=soprano..4=bass)

    // Per-note toggles
    std::atomic<float>* pNotes[12];
    static const char* NOTE_PARAM_IDS[12];

    // ── Core DSP state (autotalent) ──────────────────────────────────────────
    unsigned long mfs = 44100;
    unsigned long mcbsize = 2048;
    unsigned long mcorrsize = 1025;
    unsigned long mcbiwr = 0;
    unsigned long mcbord = 0;

    float* mcbi = nullptr;
    float* mcbf = nullptr;
    float* mcbo = nullptr;
    float* mcbwindow = nullptr;
    float* macwinv = nullptr;
    float* mhannwindow = nullptr;
    int    mnoverlap = 4;

    float* mffttime = nullptr;
    float* mfftfreqre = nullptr;
    float* mfftfreqim = nullptr;
    fft_vars* mfmembvars = nullptr;
    float* myindiff = nullptr;   // YIN cumulative-mean-normalized difference function
    float* myincumE = nullptr;   // YIN cumulative energy prefix sums

    float maref = 440.0f;
    float minpitch = 0.0f;
    float mconf = 0.0f;
    float moutpitch = 0.0f;
    float mvthresh = 0.5f;  // lower = more aggressive detection

    float mpmax = 1.0f / 70.0f;
    float mpmin = 1.0f / 700.0f;
    unsigned long mnmax = 0;
    unsigned long mnmin = 0;

    float mlfophase = 0.0f;

    // Pitch shifter
    float  mphprdd = 0.01f;
    double minphinc = 1.0;
    double moutphinc = 1.0;
    double mphincfact = 1.0;
    double mphasein = 0.0;
    double mphaseout = 0.0;
    float* mfrag = nullptr;
    unsigned long mfragsize = 0;
    float mfragFrac = 0.0f;      // sub-sample offset of the last grain extraction

    // Formant corrector
    int    mford = 7;
    float  mfalph = 0.0f;
    float  mflamb = 0.0f;
    float* mfk = nullptr;
    float* mfb = nullptr;
    float* mfc = nullptr;
    float* mfrb = nullptr;
    float* mfrc = nullptr;
    float* mfsig = nullptr;
    float* mfsmooth = nullptr;
    float  mfhp = 0.0f;
    float  mflp = 0.0f;
    float  mflpa = 0.0f;
    float** mfbuff = nullptr;
    float* mftvec = nullptr;
    float  mfmute = 1.0f;
    float  mfmutealph = 0.0f;

    // ── Modern enhancements ──────────────────────────────────────────────────
    // Humanize: track how long we've been on the same note
    float  mHumanizeTimer = 0.0f;
    float  mLastTargetPitch = 0.0f;

    // Flex-Tune: only correct if pitch error exceeds threshold
    float  mFlexSmoothPitch = 0.0f;

    // Vibrato detector: measure pitch variance over recent history
    static const int VIBRATO_HISTORY = 16;
    float  mVibratoHistory[VIBRATO_HISTORY];
    int    mVibratoIndex = 0;
    float  mVibratoDepth = 0.0f;

    // Classic mode smoothing state
    float  mClassicShift = 1.0f;

    // Correction engine (Auto-Tune-style): the Speed knob glides the CORRECTION
    // amount (semitones), not the absolute output pitch, so natural vibrato and
    // scoops pass through at slower speeds while the average pitch locks in tune.
    float mTargetCorr = 0.0f;    // correction wanted this frame (semitones)
    float mCurrentCorr = 0.0f;   // gliding correction, updated per sample
    float mGlideCoeff = 1.0f;    // one-pole glide coefficient from Speed knob
    float mShiftSemis = 0.0f;    // Shift knob, applied as a pure transpose

    // Note hysteresis: hold the current target note until the singer clearly commits
    // to another one (stops target flip-flopping between two notes = warble).
    int   mCurNote = 0;          // current target note, semitones relative to concert A
    bool  mHasNote = false;

    // 3-frame median filter on detected pitch (removes single-frame outliers/blips)
    float mPitchHist[3] = { 0.0f, 0.0f, 0.0f };
    int   mPitchHistCount = 0;

    // Voiced/unvoiced gate: cross-fade to dry on consonants/breaths (bypassed in Classic)
    float mVoicedTarget = 0.0f;  // per-frame decision: 1 = voiced, 0 = unvoiced
    float mVoicedGain = 0.0f;    // smoothed per-sample 0..1 (0 = dry passthrough, 1 = full wet)
    float mGateAtkCoeff = 1.0f;  // per-sample attack coefficient (unvoiced -> voiced)
    float mGateRelCoeff = 1.0f;  // per-sample release coefficient (voiced -> unvoiced)

    bool dspInitialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeTuneAudioProcessor)
};