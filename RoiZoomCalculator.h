#pragma once

#include <QRectF>

struct RoiZoomCommand {
    float targetYaw   = 0.f;
    float targetPitch = 0.f;
    float targetZoom  = 1.f;
};

class RoiZoomCalculator {
public:
    RoiZoomCalculator() = default;

    void setBaseFov(float hFovDeg, float vFovDeg);
    void setZoomRange(float minZoom, float maxZoom);

    RoiZoomCommand compute(float currentYaw,
                           float currentPitch,
                           float currentZoom,
                           const QRectF &normalizedRoi) const;

private:
    float m_hFov    = 62.0f;
    float m_vFov    = 37.0f;
    float m_minZoom =  1.0f;
    float m_maxZoom = 30.0f;
};
