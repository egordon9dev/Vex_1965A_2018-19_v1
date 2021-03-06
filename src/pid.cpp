#include "pid.hpp"
#include "main.h"
#include "setup.hpp"
using std::cout;
Pid_t flywheelPid, clawPid, drfbPid, DLPid, DRPid, DLTurnPid, DRTurnPid, drivePid, turnPid, curvePid;
Slew_t flywheelSlew, drfbSlew, DLSlew, DRSlew, clawSlew;
Odometry_t odometry(6.982698);

Slew_t::Slew_t() {
    slewRate = 100.0;
    output = 0;
    prevTime = millis();
}
Pid_t::Pid_t() {
    doneTime = BIL;
    DONE_ZONE = 10;
    maxIntegral = 9999999;
    dInactiveZone = iActiveZone = target = prevSensVal = sensVal = prevErr = errTot = unwind = deriv = kp = ki = kd = 0.0;
    prevTime = prevDUpdateTime = 0;
}
Odometry_t::Odometry_t(double L) {
    this->L = L;
    this->a = PI / 2;
    this->x = this->y = this->prevDL = this->prevDR = 0.0;
    this->xAxisDir = this->rotationDir = 1;
}
double Odometry_t::getX() { return x; }
double Odometry_t::getY() { return y; }
double Odometry_t::getA() { return a; }
void Odometry_t::setA(double a) { this->a = a; }
void Odometry_t::setX(double x) { this->x = x; }
void Odometry_t::setY(double y) { this->y = y; }
void Odometry_t::setXAxisDir(int n) { xAxisDir = n < 0 ? -1 : 1; }
void Odometry_t::setRotationDir(int n) { rotationDir = n < 0 ? -1 : 1; }
Point Odometry_t::getPos() {
    Point p(x, y);
    return p;
}

void Odometry_t::update() {
    double curDL = getDL(), curDR = getDR();
    double deltaDL = (curDL - this->prevDL) / ticksPerInch, deltaDR = (curDR - this->prevDR) / ticksPerInch;
    double deltaDC = (deltaDL + deltaDR) / 2.0;
    double deltaA = (deltaDR - deltaDL) / (2.0 * L);
    this->x += deltaDC * cos(a + deltaA / 2);
    this->y += deltaDC * sin(a + deltaA / 2);
    this->a += deltaA;
    prevDL = curDL;
    prevDR = curDR;
}

/*
  in: input voltage
*/
double Slew_t::update(double in) {
    int dt = millis() - prevTime;
    if (dt > 1000) dt = 0;
    prevTime = millis();
    double maxIncrease = slewRate * dt;
    double outputRate = (double)(in - output) / (double)dt;
    if (fabs(outputRate) < slewRate) {
        output = in;
    } else if (outputRate > 0) {
        output += maxIncrease;
    } else {
        output -= maxIncrease;
    }
    return output;
}
// proportional + integral + derivative control feedback
double Pid_t::update() {
    int dt = millis() - prevTime;
    if (dt > 1000) dt = 0;
    prevTime = millis();
    // PROPORTIONAL
    double err = target - sensVal;
    double p = err * kp;
    // DERIVATIVE
    double d = deriv;  // set d to old derivative
    double derivativeDt = millis() - prevDUpdateTime;
    if (derivativeDt > 1000) {
        prevSensVal = sensVal;
        prevDUpdateTime = millis();
    } else if (derivativeDt >= 15) {
        d = ((prevSensVal - sensVal) * kd) / derivativeDt;
        prevDUpdateTime = millis();
        deriv = d;  // save new derivative
        prevSensVal = sensVal;
    }
    if (fabs(err) < dInactiveZone) d = 0;
    // INTEGRAL
    errTot += err * dt;
    if (fabs(err) > iActiveZone) errTot = 0;
    if (fabs(d) > 10) {
        double maxErrTot = maxIntegral / ki;
        if (errTot > maxErrTot) errTot = maxErrTot;
        if (errTot < -maxErrTot) errTot = -maxErrTot;
    }
    if ((err > 0.0 && errTot < 0.0) || (err < 0.0 && errTot > 0.0) || abs(err) < 0.001) {
        if (fabs(err) - unwind > -0.001) {
            errTot = 0.0;
            // printf("UNWIND\n");
        }
    }
    if (fabs(unwind) < 0.001 && fabs(err) < 0.001) {
        errTot = 0.0;
        // printf("UNWIND\n");
    }
    double i = errTot * ki;
    // done zone
    if (fabs(err) <= DONE_ZONE && doneTime > millis()) {
        doneTime = millis();
        // printf("DONE\n");
    }
    // derivative action: slowing down
    /*if (fabs(d) > (fabs(p)) * 20.0) {
          errTot = 0.0;
      }*/
    prevErr = err;
    // printf("p: %lf, i: %lf, d: %lf\t", p, i, d);
    // OUTPUT
    return p + i + d;
}
Point g_target;
namespace driveData {
Point target;
int wait;
int doneT;
void init(Point t, int w) {
    target = t;
    wait = w;
    doneT = BIL;
}
}  // namespace driveData
void pidDriveInit(Point target, const int wait) {
    // prevent div by 0 errors
    if (target.x == 0.0) target.x = 0.001;
    if (target.y == 0.0) target.y = 0.001;
    drivePid.doneTime = BIL;
    turnPid.doneTime = BIL;
    driveData::init(target, wait);
}
bool pidDrive() {
    using driveData::doneT;
    using driveData::target;
    using driveData::wait;
    g_target = target;
    Point pos(odometry.getX(), odometry.getY());
    static Point prevPos(0, 0);
    Point targetDir = target - pos;
    // error detection
    Point dirOrientation(cos(odometry.getA()), sin(odometry.getA()));
    double aErr = acos(clamp((dirOrientation * targetDir) / (dirOrientation.mag() * targetDir.mag()), -1.0, 1.0));

    // allow for driving backwards
    int driveDir = 1;
    if (aErr > PI / 2) {
        driveDir = -1;
        aErr = PI - aErr;
    }
    if (dirOrientation < targetDir) aErr *= -driveDir;
    if (dirOrientation > targetDir) aErr *= driveDir;

    // error correction
    double curA = odometry.getA();
    drivePid.target = 0.0;
    drivePid.sensVal = targetDir.mag() * cos(aErr);
    if (drivePid.sensVal < 4) aErr = 0;
    turnPid.target = 0;
    turnPid.sensVal = aErr;
    int turnPwr = clamp((int)turnPid.update(), -8000, 8000);
    int drivePwr = clamp((int)drivePid.update(), -8000, 8000);
    // prevent turn saturation
    // if (abs(turnPwr) > 0.2 * abs(drivePwr)) turnPwr = (turnPwr < 0 ? -1 : 1) * 0.2 * abs(drivePwr);
    setDL(-drivePwr * driveDir - turnPwr);
    setDR(-drivePwr * driveDir + turnPwr);

    if (fabs(drivePid.sensVal) < 1 && (pos - prevPos).mag() < 0.01) {
        if (doneT > millis()) doneT = millis();
    } else {
        doneT = BIL;
    }
    prevPos = pos;
    return doneT + wait < millis();
}
namespace driveLineData {
Point start, target, delta;
int wait;
int doneT;
void init(Point s, Point t, int w) {
    start = s;
    target = t;
    delta = target - start;
    wait = w;
    doneT = BIL;
}
}  // namespace driveLineData
void pidDriveLineInit(Point start, Point target, const int wait) {
    // prevent div by 0 errors
    if (start.x == 0.0) start.x = 0.001;
    if (start.y == 0.0) start.y = 0.001;
    if (target.x == 0.0) target.x = 0.001;
    if (target.y == 0.0) target.y = 0.001;
    drivePid.doneTime = BIL;
    turnPid.doneTime = BIL;
    driveLineData::init(start, target, wait);
}
bool pidDriveLine() {
    using driveLineData::delta;
    using driveLineData::doneT;
    using driveLineData::start;
    using driveLineData::target;
    using driveLineData::wait;
    Point pos(odometry.getX(), odometry.getY());
    static Point prevPos(0, 0);
    Point toTarget = target - pos;
    double a = PI / 2 - atan(0.2 * (toTarget * delta) / toTarget.magCross(delta));
    if (toTarget < delta) a *= -1;
    Point targetDir = polarToRect(1, a);
    double curA = odometry.getA();
    Point orientationDir(cos(curA), sin(curA));
    double aErr = (orientationDir * targetDir) / (orientationDir.mag() * targetDir.mag());
    // error detection
    // allow for driving backwards
    int driveDir = 1;
    if (aErr > PI / 2) {
        driveDir = -1;
        aErr = PI - aErr;
    }
    if (orientationDir < toTarget) aErr *= -driveDir;
    if (orientationDir > toTarget) aErr *= driveDir;

    // error correction
    turnPid.sensVal = aErr;
    turnPid.target = 0.0;
    drivePid.target = 0.0;
    drivePid.sensVal = toTarget.mag() * cos(aErr);
    int turnPwr = clamp((int)turnPid.update(), -8000, 8000);
    int drivePwr = clamp((int)drivePid.update(), -8000, 8000);
    // prevent turn saturation
    // if (abs(turnPwr) > 0.2 * abs(drivePwr)) turnPwr = (turnPwr < 0 ? -1 : 1) * 0.2 * abs(drivePwr);
    setDL(-drivePwr * driveDir - turnPwr);
    setDR(-drivePwr * driveDir + turnPwr);

    if (fabs(drivePid.sensVal) < 1 && (pos - prevPos).mag() < 0.01) {
        if (doneT > millis()) doneT = millis();
    } else {
        doneT = BIL;
    }
    prevPos = pos;
    return doneT + wait < millis();
}

int g_pidTurnLimit = 12000;
namespace turnData {
double angle;
int wait;
int doneT;
void init(double a, int w) {
    angle = a;
    wait = w;
    doneT = BIL;
}
}  // namespace turnData
void pidTurnInit(const double angle, const int wait) {
    turnData::init(angle, wait);
    turnPid.doneTime = BIL;
}
bool pidTurn() {
    using turnData::angle;
    using turnData::doneT;
    using turnData::wait;
    turnPid.sensVal = odometry.getA();
    turnPid.target = angle;
    int pwr = clamp((int)turnPid.update(), -g_pidTurnLimit, g_pidTurnLimit);
    setDL(-pwr);
    setDR(pwr);
    static double prevA = turnPid.sensVal;
    // printf("%f %f\n", fabs(turnPid.sensVal - turnPid.target), fabs(turnPid.sensVal - prevA));
    if (fabs(turnPid.sensVal - turnPid.target) < 0.1 && fabs(turnPid.sensVal - prevA) < 0.001) {
        if (doneT > millis()) doneT = millis();
    } else {
        doneT = BIL;
    }
    prevA = turnPid.sensVal;
    return doneT + wait < millis();
}
bool pidTurnSweep(double tL, double tR, int wait) {
    DLPid.sensVal = getDL();
    DRPid.sensVal = getDR();
    DLPid.target = tL * ticksPerInch;
    DRPid.target = tR * ticksPerInch;
    setDL(DLPid.update());
    setDR(DRPid.update());
    if (DLPid.doneTime + wait < millis() && DRPid.doneTime + wait < millis()) return true;
    return false;
}
namespace arcData {
const int pwrLim1 = 7500, pwrLim2 = pwrLim1 + 1000;
int doneT;
Point center;
Point _target, _start;
double _rMag;
int _rotationDirection;
int wait;
int bias;
bool followArc;
void init(Point start, Point target, double rMag, int rotationDirection) {
    if (target.x == 0.0) target.x = 0.001;
    if (target.y == 0.0) target.y = 0.001;
    if (start.x == 0.0) start.x = 0.001;
    if (start.y == 0.0) start.y = 0.001;

    doneT = BIL;
    _start = start;
    _target = target;
    _rotationDirection = rotationDirection;
    _rMag = rMag;
    bias = 0;
    followArc = false;

    Point deltaPos = target - start;
    Point midPt((start.x + target.x) / 2.0, (start.y + target.y) / 2.0);
    double altAngle = atan2(deltaPos.y, deltaPos.x) + (PI / 2) * rotationDirection;
    double altMag = sqrt(clamp(pow(rMag, 2) - pow(deltaPos.mag() / 2, 2), 0.0, 999999999.9));
    center = midPt + polarToRect(altMag, altAngle);
}
// estimate the distance remaining for the drive
// assumes you would never arc more than about 7*PI / 4
double getArcPos() {
    Point pos = odometry.getPos() - center;
    Point tgt = _target - center;
    Point st = _start - center;
    double a = acos(clamp((pos * tgt) / (pos.mag() * tgt.mag()), -1.0, 1.0));
    if (_rotationDirection == 1) {
        if (pos < st && a > PI / 2) a = 2 * PI - a;
    } else {
        if (pos > st && a > PI / 2) a = 2 * PI - a;
    }
    return a * pos.mag();
}
}  // namespace arcData

void printArcData() {
    printf("%.1f DL%d DR%d drive %3.1f/%3.1f curve %2.3f/%2.3f R %.1f/%.1f x %3.1f/%3.1f y %3.1f/%3.1f a %.1f\n", millis() / 1000.0, (int)(getDLVoltage() / 100 + 0.5), (int)(getDRVoltage() / 100 + 0.5), drivePid.sensVal, drivePid.target, curvePid.sensVal, curvePid.target, (odometry.getPos() - arcData::center).mag(), arcData::_rMag, odometry.getX(), arcData::_target.x, odometry.getY(), arcData::_target.y, odometry.getA());
    std::cout << std::endl;
}
void pidDriveArcInit(Point start, Point target, double rMag, int rotationDir, int wait) {
    drivePid.doneTime = BIL;
    turnPid.doneTime = BIL;
    curvePid.doneTime = BIL;
    arcData::init(start, target, rMag, rotationDir);
    arcData::wait = wait;
}

void pidFollowArcInit(Point start, Point target, double rMag, int rotationDir, int wait) {
    pidDriveArcInit(start, target, rMag, rotationDir, wait);
    arcData::followArc = true;
}
void pidDriveArcBias(int b) { arcData::bias = b; }
bool pidDriveArc() {
    using arcData::_rMag;
    using arcData::_rotationDirection;
    using arcData::_start;
    using arcData::_target;
    using arcData::center;
    using arcData::doneT;
    using arcData::pwrLim1;
    using arcData::pwrLim2;
    using arcData::wait;
    Point pos = odometry.getPos();
    double arcPos = arcData::getArcPos();

    Point rVector = center - pos;
    Point targetVector = rVector.rotate(-_rotationDirection);
    Point orientationVector(cos(odometry.getA()), sin(odometry.getA()));
    double x = (targetVector * orientationVector) / (targetVector.mag() * orientationVector.mag());
    double errAngle = acos(clamp(x, -1.0, 1.0));
    double errRadius = rVector.mag() - _rMag;
    // allow for driving backwards and allow for negative errAngle values
    int driveDir = 1;
    if (errAngle > PI / 2) {
        driveDir = -1;
        errAngle = PI - errAngle;
    }
    if (orientationVector < targetVector) errAngle *= -driveDir;
    if (orientationVector > targetVector) errAngle *= driveDir;
    // printf("center: %.1f,%.1f pos: %.1f,%.1ftarget:%.1f,%.1f\n", center.x, center.y, pos.x, pos.y, arcData::_target.x, arcData::_target.y);
    // error correction
    curvePid.sensVal = errAngle /*- clamp(errRadius * _rotationDirection * (PI / 6), -PI / 3, PI / 3)*/;
    curvePid.target = 0;
    Point rVec = pos - center, tgt = _target - center;
    if (_rotationDirection == 1) {
        if (rVec > tgt) arcPos *= -1;
    } else {
        if (rVec < tgt) arcPos *= -1;
    }
    if (fabs(arcPos) > rVec.mag() * PI) arcPos *= -1;
    drivePid.sensVal = arcPos;
    drivePid.target = 0;
    double drivePwr = -drivePid.update();
    if (arcData::followArc) drivePwr = pwrLim1;
    int turnPwr = clamp((int)curvePid.update(), -pwrLim1, pwrLim1);
    double pwrFactor = 1;  // clamp(1.0 / (1.0 + fabs(errRadius) / 2.0) * 1.0 / (1.0 + fabs(errAngle) * 6), 0.5, 1.0);
    double curveFac = clamp(2.0 / (1.0 + exp(-_rMag / 7.0)) - 1.0, 0.001, 1.0);
    double dlOut = clamp(drivePwr * pwrFactor * driveDir, (double)-pwrLim1, (double)pwrLim1);
    double drOut = clamp(drivePwr * pwrFactor * driveDir, (double)-pwrLim1, (double)pwrLim1);
    if (_rotationDirection * driveDir == -1) {
        dlOut *= 1.0 / curveFac;
        drOut *= curveFac;
    } else {
        dlOut *= curveFac;
        drOut *= 1.0 / curveFac;
    }
    dlOut -= _rotationDirection * driveDir * arcData::bias;
    drOut += _rotationDirection * driveDir * arcData::bias;
    setDL(clamp((int)dlOut, -pwrLim2, pwrLim2) - turnPwr);
    setDR(clamp((int)drOut, -pwrLim2, pwrLim2) + turnPwr);
    static Point prevPos(0, 0);

    if (fabs(drivePid.sensVal) < 2 && (pos - prevPos).mag() < 0.01) {
        if (doneT > millis()) doneT = millis();
    } else {
        doneT = BIL;
    }
    prevPos = pos;
    return doneT + wait < millis();
}