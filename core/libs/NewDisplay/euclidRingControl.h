#ifndef LIBS_NEWDISPLAY_EUCLIDRINGCONTROL_H_
#define LIBS_NEWDISPLAY_EUCLIDRINGCONTROL_H_

#include "displayControls.h"

// Data for the Euclidean ring: one dot per step, pulses lit and joined into
// a polygon, the playhead outlined. When a change is waiting for the next
// bar, the pending pattern is drawn as an inner ring.
struct strEuclidRingData
{
    uint8_t steps = 16;
    uint8_t pulses = 0;
    uint8_t rotation = 0;
    int8_t playhead = -1; // -1 when stopped

    bool hasPending = false;
    uint8_t pendingSteps = 16;
    uint8_t pendingPulses = 0;
    uint8_t pendingRotation = 0;

    bool enabled = true;
    bool muted = false;

    const char* centerText = nullptr;  // e.g. "E(4,16)"
    const char* centerText2 = nullptr; // e.g. "next bar: E(5,16)"
};

class cEuclidRing : public cDisplayControl
{
  public:
    cEuclidRing(strControlProperties* properties = nullptr);
    virtual ~cEuclidRing();

    virtual void update();

    virtual void setStyle(uint32_t style);
    virtual void setText(const char* text);
    virtual void setValue(int value);
    virtual void setColors(const uint32_t* colors);
    virtual void setData(void* data);

  private:
    void drawRing(int16_t cx, int16_t cy, int16_t r, uint8_t steps, uint8_t pulses,
                  uint8_t rotation, int8_t playhead, bool dim);

    strEuclidRingData* data = nullptr;
    int16_t fontHandle = 0;
    int16_t smallFontHandle = 0;
};

#endif /* LIBS_NEWDISPLAY_EUCLIDRINGCONTROL_H_ */
