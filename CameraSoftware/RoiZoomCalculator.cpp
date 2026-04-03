#include "RoiZoomCalculator.h"
#include <algorithm>
#include <cmath>

void RoiZoomCalculator::setBaseFov(float hFovDeg, float vFovDeg)
{
    m_hFov = hFovDeg;
    m_vFov = vFovDeg;
}

void RoiZoomCalculator::setZoomRange(float minZoom, float maxZoom)
{
    m_minZoom = minZoom;
    m_maxZoom = maxZoom;
}

RoiZoomCommand RoiZoomCalculator::compute(float currentYaw,
                                          float currentPitch,
                                          float currentZoom,
                                          const QRectF &normalizedRoi) const
{
    RoiZoomCommand cmd;

    // Effective FOV at current zoom
    float effHFov = m_hFov / std::max(currentZoom, 1.0f);
    float effVFov = m_vFov / std::max(currentZoom, 1.0f);

    // Center offset from frame center (0.5, 0.5)
    QPointF roiCenter = normalizedRoi.center();
    float offsetX = static_cast<float>(roiCenter.x()) - 0.5f;
    float offsetY = static_cast<float>(roiCenter.y()) - 0.5f;

    // Convert pixel offset to angle offset
    float deltaYaw   =  offsetX * effHFov;
    float deltaPitch = -offsetY * effVFov;

    // Compute target angles
    cmd.targetYaw   = currentYaw   + deltaYaw;
    cmd.targetPitch = currentPitch + deltaPitch;

    // Compute target zoom to fill frame with selected region
    float roiSpan = static_cast<float>(std::max(normalizedRoi.width(),
                                                normalizedRoi.height()));
    if (roiSpan < 0.01f) roiSpan = 0.01f;

    float desiredZoom = currentZoom / roiSpan;
    desiredZoom *= 0.9f; // small margin

    cmd.targetZoom = std::clamp(desiredZoom, m_minZoom, m_maxZoom);

    return cmd;
}
