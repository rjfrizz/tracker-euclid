#ifndef INTERFACE_EUCLIDPARAMS_H_
#define INTERFACE_EUCLIDPARAMS_H_

#include "core/modulesBase.h"
#include "euclidRingControl.h"

// EUCLID home screen: the selected channel's ring on the left, its settings
// on the right, and the 8 channels along the bottom over the screen keys.
class cEuclidParams : public ModuleBase
{
  public:
    enum Param : uint8_t
    {
        paramSteps = 0,
        paramPulses,
        paramRotation,
        paramDivision,
        paramVelocity,
        paramGate,
        paramChance,
        paramMidiChannel,
        paramEnabled,
        paramCount
    };

    virtual void update();
    virtual void start(uint32_t options);
    virtual void stop();

    virtual void initDisplayControls();
    virtual void destroyDisplayControls() {}

    cEuclidParams() {}
    ~cEuclidParams() {}

    void setDefaultScreenFunct();
    void showDefaultScreen();

    void refreshRing();
    void refreshParams();
    void refreshParam(uint8_t param);
    void refreshChannels();
    void refreshFrame();
    void refreshTitle();

    void selectChannel(uint8_t ch);
    void moveFocus(int8_t delta);
    void changeParam(int16_t delta);
    void toggleMute(uint8_t ch);

    uint8_t selectedChannel = 0;
    uint8_t selectedParam = paramPulses;

    hControl ring = -1;
    hControl paramName[paramCount];
    hControl paramValue[paramCount];
    hControl channelLabel[8];
    hControl bgLabel = -1;
    hControl frame = -1;

    strEuclidRingData ringData;
    strFrameData frameData;

    char ringText[16];
    char ringText2[32];
    char valueText[paramCount][12];
    char channelText[8][6];
    char channelText2[8][12];
    char titleText[24];

    int8_t lastPlayhead = -2;
    bool lastPending = false;
    bool lastRunning = false;
};

extern cEuclidParams euclidParams;

#endif /* INTERFACE_EUCLIDPARAMS_H_ */
