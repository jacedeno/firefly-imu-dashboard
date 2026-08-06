#pragma once

// Madgwick sensor fusion filter (6-DOF: accel + gyro)
// Produces a unit quaternion representing orientation.

#include <math.h>

class MadgwickFilter {
public:
    MadgwickFilter(float beta = 0.1f, float sampleFreq = 200.0f)
        : _beta(beta), _sampleFreq(sampleFreq),
          _q0(1.0f), _q1(0.0f), _q2(0.0f), _q3(0.0f) {}

    void update(float gx, float gy, float gz, float ax, float ay, float az) {
        float q0 = _q0, q1 = _q1, q2 = _q2, q3 = _q3;

        // Rate of change from gyroscope
        float qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        float qDot2 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
        float qDot3 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
        float qDot4 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

        // Compute feedback only if accelerometer measurement valid
        float aNorm = sqrtf(ax * ax + ay * ay + az * az);
        if (aNorm > 0.0f) {
            // Normalize accelerometer measurement
            float recipNorm = 1.0f / aNorm;
            ax *= recipNorm;
            ay *= recipNorm;
            az *= recipNorm;

            // Auxiliary variables to avoid repeated arithmetic
            float _2q0 = 2.0f * q0;
            float _2q1 = 2.0f * q1;
            float _2q2 = 2.0f * q2;
            float _2q3 = 2.0f * q3;
            float _4q0 = 4.0f * q0;
            float _4q1 = 4.0f * q1;
            float _4q2 = 4.0f * q2;
            float _8q1 = 8.0f * q1;
            float _8q2 = 8.0f * q2;
            float q0q0 = q0 * q0;
            float q1q1 = q1 * q1;
            float q2q2 = q2 * q2;
            float q3q3 = q3 * q3;

            // Gradient descent corrective step
            float s0 = _4q0 * q2q2 + _2q2 * ax + _4q0 * q1q1 - _2q1 * ay;
            float s1 = _4q1 * q3q3 - _2q3 * ax + 4.0f * q0q0 * q1 - _2q0 * ay - _4q1 + _8q1 * q1q1 + _8q1 * q2q2 + _4q1 * az;
            float s2 = 4.0f * q0q0 * q2 + _2q0 * ax + _4q2 * q3q3 - _2q3 * ay - _4q2 + _8q2 * q1q1 + _8q2 * q2q2 + _4q2 * az;
            float s3 = 4.0f * q1q1 * q3 - _2q1 * ax + 4.0f * q2q2 * q3 - _2q2 * ay;

            // Normalize step magnitude
            recipNorm = 1.0f / sqrtf(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);
            s0 *= recipNorm;
            s1 *= recipNorm;
            s2 *= recipNorm;
            s3 *= recipNorm;

            // Apply feedback step
            qDot1 -= _beta * s0;
            qDot2 -= _beta * s1;
            qDot3 -= _beta * s2;
            qDot4 -= _beta * s3;
        }

        // Integrate rate of change to yield quaternion
        float dt = 1.0f / _sampleFreq;
        q0 += qDot1 * dt;
        q1 += qDot2 * dt;
        q2 += qDot3 * dt;
        q3 += qDot4 * dt;

        // Normalize quaternion
        float recipNorm = 1.0f / sqrtf(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);
        _q0 = q0 * recipNorm;
        _q1 = q1 * recipNorm;
        _q2 = q2 * recipNorm;
        _q3 = q3 * recipNorm;
    }

    // 9-DOF update (accel + gyro + magnetometer). Units: gyro rad/s; accel and
    // mag any consistent units (both are normalized internally). Falls back to
    // the 6-DOF update if the magnetometer sample is all-zero.
    void updateMag(float gx, float gy, float gz, float ax, float ay, float az,
                   float mx, float my, float mz) {
        if (mx == 0.0f && my == 0.0f && mz == 0.0f) { update(gx, gy, gz, ax, ay, az); return; }

        float q0 = _q0, q1 = _q1, q2 = _q2, q3 = _q3;
        float qDot1 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        float qDot2 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
        float qDot3 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
        float qDot4 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

        float aNorm = sqrtf(ax * ax + ay * ay + az * az);
        if (aNorm > 0.0f) {
            float recipNorm = 1.0f / aNorm;
            ax *= recipNorm; ay *= recipNorm; az *= recipNorm;
            recipNorm = 1.0f / sqrtf(mx * mx + my * my + mz * mz);
            mx *= recipNorm; my *= recipNorm; mz *= recipNorm;

            float _2q0mx = 2.0f * q0 * mx, _2q0my = 2.0f * q0 * my, _2q0mz = 2.0f * q0 * mz, _2q1mx = 2.0f * q1 * mx;
            float _2q0 = 2.0f * q0, _2q1 = 2.0f * q1, _2q2 = 2.0f * q2, _2q3 = 2.0f * q3;
            float _2q0q2 = 2.0f * q0 * q2, _2q2q3 = 2.0f * q2 * q3;
            float q0q0 = q0*q0, q0q1 = q0*q1, q0q2 = q0*q2, q0q3 = q0*q3;
            float q1q1 = q1*q1, q1q2 = q1*q2, q1q3 = q1*q3;
            float q2q2 = q2*q2, q2q3 = q2*q3, q3q3 = q3*q3;

            float hx = mx*q0q0 - _2q0my*q3 + _2q0mz*q2 + mx*q1q1 + _2q1*my*q2 + _2q1*mz*q3 - mx*q2q2 - mx*q3q3;
            float hy = _2q0mx*q3 + my*q0q0 - _2q0mz*q1 + _2q1mx*q2 - my*q1q1 + my*q2q2 + _2q2*mz*q3 - my*q3q3;
            float _2bx = sqrtf(hx*hx + hy*hy);
            float _2bz = -_2q0mx*q2 + _2q0my*q1 + mz*q0q0 + _2q1mx*q3 - mz*q1q1 + _2q2*my*q3 - mz*q2q2 + mz*q3q3;
            float _4bx = 2.0f * _2bx, _4bz = 2.0f * _2bz;

            float s0 = -_2q2*(2.0f*q1q3 - _2q0q2 - ax) + _2q1*(2.0f*q0q1 + _2q2q3 - ay) - _2bz*q2*(_2bx*(0.5f-q2q2-q3q3)+_2bz*(q1q3-q0q2)-mx) + (-_2bx*q3+_2bz*q1)*(_2bx*(q1q2-q0q3)+_2bz*(q0q1+q2q3)-my) + _2bx*q2*(_2bx*(q0q2+q1q3)+_2bz*(0.5f-q1q1-q2q2)-mz);
            float s1 = _2q3*(2.0f*q1q3 - _2q0q2 - ax) + _2q0*(2.0f*q0q1 + _2q2q3 - ay) - 4.0f*q1*(1.0f-2.0f*q1q1-2.0f*q2q2-az) + _2bz*q3*(_2bx*(0.5f-q2q2-q3q3)+_2bz*(q1q3-q0q2)-mx) + (_2bx*q2+_2bz*q0)*(_2bx*(q1q2-q0q3)+_2bz*(q0q1+q2q3)-my) + (_2bx*q3-_4bz*q1)*(_2bx*(q0q2+q1q3)+_2bz*(0.5f-q1q1-q2q2)-mz);
            float s2 = -_2q0*(2.0f*q1q3 - _2q0q2 - ax) + _2q3*(2.0f*q0q1 + _2q2q3 - ay) - 4.0f*q2*(1.0f-2.0f*q1q1-2.0f*q2q2-az) + (-_4bx*q2-_2bz*q0)*(_2bx*(0.5f-q2q2-q3q3)+_2bz*(q1q3-q0q2)-mx) + (_2bx*q1+_2bz*q3)*(_2bx*(q1q2-q0q3)+_2bz*(q0q1+q2q3)-my) + (_2bx*q0-_4bz*q2)*(_2bx*(q0q2+q1q3)+_2bz*(0.5f-q1q1-q2q2)-mz);
            float s3 = _2q1*(2.0f*q1q3 - _2q0q2 - ax) + _2q2*(2.0f*q0q1 + _2q2q3 - ay) + (-_4bx*q3+_2bz*q1)*(_2bx*(0.5f-q2q2-q3q3)+_2bz*(q1q3-q0q2)-mx) + (-_2bx*q0+_2bz*q2)*(_2bx*(q1q2-q0q3)+_2bz*(q0q1+q2q3)-my) + _2bx*q1*(_2bx*(q0q2+q1q3)+_2bz*(0.5f-q1q1-q2q2)-mz);

            recipNorm = 1.0f / sqrtf(s0*s0 + s1*s1 + s2*s2 + s3*s3);
            s0 *= recipNorm; s1 *= recipNorm; s2 *= recipNorm; s3 *= recipNorm;

            qDot1 -= _beta * s0; qDot2 -= _beta * s1; qDot3 -= _beta * s2; qDot4 -= _beta * s3;
        }

        float dt = 1.0f / _sampleFreq;
        q0 += qDot1 * dt; q1 += qDot2 * dt; q2 += qDot3 * dt; q3 += qDot4 * dt;
        float recipNorm = 1.0f / sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3);
        _q0 = q0 * recipNorm; _q1 = q1 * recipNorm; _q2 = q2 * recipNorm; _q3 = q3 * recipNorm;
    }

    void setSampleFreq(float hz) { if (hz > 1.0f) _sampleFreq = hz; }

    float w() const { return _q0; }
    float x() const { return _q1; }
    float y() const { return _q2; }
    float z() const { return _q3; }

private:
    float _beta;
    float _sampleFreq;
    float _q0, _q1, _q2, _q3;
};


// Mahony AHRS filter. Unlike Madgwick it has an integral term (Ki) that
// estimates and removes gyro bias online, so heading holds without a perfect
// boot-time gyro calibration and without yaw drift. Kp = responsiveness.
class MahonyFilter {
public:
    MahonyFilter(float kp = 0.4f, float ki = 0.08f, float sampleFreq = 119.0f)
        : _twoKp(2.0f * kp), _twoKi(2.0f * ki), _freq(sampleFreq), _dt(1.0f / sampleFreq),
          _q0(1.0f), _q1(0.0f), _q2(0.0f), _q3(0.0f),
          _ibx(0.0f), _iby(0.0f), _ibz(0.0f) {}

    void setSampleFreq(float hz) { if (hz > 1.0f) { _freq = hz; _dt = 1.0f / hz; } }

    // Per-iteration timestep, measured by the caller. The nominal rate is only a
    // fallback: the real loop runs 107-118 Hz against a nominal 119, so a fixed
    // dt under-integrates the gyro by ~5% and every rotation falls short.
    void setDt(float dt) { if (dt > 1e-5f && dt < 0.1f) _dt = dt; }

    // Gravity is the only tilt reference, so a sample must actually look like
    // gravity. While the board is being moved by hand, |a| departs from 1 g and
    // the "down" it implies is wrong — in 9-DOF that tilt error couples straight
    // into heading. Rejecting those samples is what stops a rotation from
    // permanently displacing yaw.
    static bool accelUsable(float ax, float ay, float az) {
        float n2 = ax*ax + ay*ay + az*az;             // in g
        return n2 > 0.5625f && n2 < 1.5625f;          // 0.75 g .. 1.25 g
    }

    void update(float gx, float gy, float gz, float ax, float ay, float az) {
        float q0 = _q0, q1 = _q1, q2 = _q2, q3 = _q3;
        if (accelUsable(ax, ay, az)) {
            float rn = 1.0f / sqrtf(ax*ax + ay*ay + az*az); ax*=rn; ay*=rn; az*=rn;
            float hvx = q1*q3 - q0*q2, hvy = q0*q1 + q2*q3, hvz = q0*q0 - 0.5f + q3*q3;
            float ex = ay*hvz - az*hvy, ey = az*hvx - ax*hvz, ez = ax*hvy - ay*hvx;
            applyFeedback(gx, gy, gz, ex, ey, ez);
        }
        integrate(q0, q1, q2, q3, gx, gy, gz);
    }

    void updateMag(float gx, float gy, float gz, float ax, float ay, float az,
                   float mx, float my, float mz) {
        if (mx == 0.0f && my == 0.0f && mz == 0.0f) { update(gx, gy, gz, ax, ay, az); return; }
        float q0 = _q0, q1 = _q1, q2 = _q2, q3 = _q3;
        if (accelUsable(ax, ay, az)) {
            float rn = 1.0f / sqrtf(ax*ax + ay*ay + az*az); ax*=rn; ay*=rn; az*=rn;
            rn = 1.0f / sqrtf(mx*mx + my*my + mz*mz); mx*=rn; my*=rn; mz*=rn;
            float q0q0=q0*q0,q0q1=q0*q1,q0q2=q0*q2,q0q3=q0*q3,q1q1=q1*q1,q1q2=q1*q2,q1q3=q1*q3,q2q2=q2*q2,q2q3=q2*q3,q3q3=q3*q3;
            float hx = 2.0f*(mx*(0.5f-q2q2-q3q3) + my*(q1q2-q0q3) + mz*(q1q3+q0q2));
            float hy = 2.0f*(mx*(q1q2+q0q3) + my*(0.5f-q1q1-q3q3) + mz*(q2q3-q0q1));
            float bx = sqrtf(hx*hx + hy*hy);
            float bz = 2.0f*(mx*(q1q3-q0q2) + my*(q2q3+q0q1) + mz*(0.5f-q1q1-q2q2));
            float hvx = q1q3-q0q2, hvy = q0q1+q2q3, hvz = q0q0-0.5f+q3q3;
            float hwx = bx*(0.5f-q2q2-q3q3) + bz*(q1q3-q0q2);
            float hwy = bx*(q1q2-q0q3) + bz*(q0q1+q2q3);
            float hwz = bx*(q0q2+q1q3) + bz*(0.5f-q1q1-q2q2);
            float ex = (ay*hvz-az*hvy) + (my*hwz-mz*hwy);
            float ey = (az*hvx-ax*hvz) + (mz*hwx-mx*hwz);
            float ez = (ax*hvy-ay*hvx) + (mx*hwy-my*hwx);
            applyFeedback(gx, gy, gz, ex, ey, ez);
        }
        integrate(q0, q1, q2, q3, gx, gy, gz);
    }

    float w() const { return _q0; }
    float x() const { return _q1; }
    float y() const { return _q2; }
    float z() const { return _q3; }

private:
    void applyFeedback(float &gx, float &gy, float &gz, float ex, float ey, float ez) {
        if (_twoKi > 0.0f) {
            _ibx += _twoKi * ex * _dt; _iby += _twoKi * ey * _dt; _ibz += _twoKi * ez * _dt;
            // Clamp the integral. It models gyro bias, which for this part is a
            // few dps at most, so anything larger is windup from a transient —
            // and unclamped windup takes tens of seconds to unwind, which is
            // exactly the slow "hunting" seen after every rotation.
            _ibx = clampBias(_ibx); _iby = clampBias(_iby); _ibz = clampBias(_ibz);
            gx += _ibx; gy += _iby; gz += _ibz;
        }
        gx += _twoKp * ex; gy += _twoKp * ey; gz += _twoKp * ez;
    }
    static float clampBias(float v) {
        const float LIM = 0.05f;   // rad/s ~= 2.9 dps
        return v > LIM ? LIM : (v < -LIM ? -LIM : v);
    }
    void integrate(float q0, float q1, float q2, float q3, float gx, float gy, float gz) {
        float dt = _dt; gx *= 0.5f*dt; gy *= 0.5f*dt; gz *= 0.5f*dt;
        float qa = q0, qb = q1, qc = q2;
        q0 += (-qb*gx - qc*gy - q3*gz);
        q1 += ( qa*gx + qc*gz - q3*gy);
        q2 += ( qa*gy - qb*gz + q3*gx);
        q3 += ( qa*gz + qb*gy - qc*gx);
        float rn = 1.0f / sqrtf(q0*q0 + q1*q1 + q2*q2 + q3*q3);
        _q0 = q0*rn; _q1 = q1*rn; _q2 = q2*rn; _q3 = q3*rn;
    }
    float _twoKp, _twoKi, _freq, _dt;
    float _q0, _q1, _q2, _q3;
    float _ibx, _iby, _ibz;
};
