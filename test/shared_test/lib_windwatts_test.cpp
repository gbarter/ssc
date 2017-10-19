#include <gtest\gtest.h>
#include <iostream>
#include <vector>

#include <lib_windwatts.h>

/**
 * TurbinePowerTest checks individual turbine power output calculated by wind_power_calculator.
 * SetUp() creates a wpc with default values for single-value variables (shear exponent), and
 * a test power curve with 41 entries: after the cut-in speed of 6 m/s, the curve is linear, then 0
 * at 25 m/s.
 */

class TurbinePowerTest : public ::testing::Test{
protected:
	wind_power_calculator wpc;
	int powerCurveSteps = 41;

public:
	double e = 0.001;
	void SetUp(){
		std::vector<double> powerCurve, densityCorrectedSpeeds;
		densityCorrectedSpeeds.resize(powerCurveSteps);
		for (unsigned int i = 0; i < powerCurveSteps; i++){
			if (i < 6) powerCurve.push_back(0);
			else if (i > 25) powerCurve.push_back(0);
			else powerCurve.push_back(i - 5);
		}
		wpc.m_dShearExponent = 0.14;
		wpc.m_dRotorDiameter = 77;
		wpc.m_adDensityCorrectedWS = densityCorrectedSpeeds;
		wpc.m_adPowerCurveKW = powerCurve;
		wpc.m_dHubHeight = 80;
		wpc.m_dMeasurementHeight = 80;
		wpc.m_iLengthOfTurbinePowerCurveArray = powerCurveSteps;
		wpc.m_dLossesPercent = 0;
		wpc.m_dLossesAbsolute = 0;
	}
};

// air density is 1
TEST_F(TurbinePowerTest, simpleCase){
	double airDensity = 1.0;
	
}

TEST_F(TurbinePowerTest, interpolateWSCase){

}

/**
* SimpleWake tests check wind farm outputs calculated by wind_power_calculator.
* SetUp() creates and initializes a wind_power_calculator with test settings and weather inputs.
*/
