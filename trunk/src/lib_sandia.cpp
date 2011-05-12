#include <math.h>
#include "lib_sandia.h"

static double sandia_voc( double Tc, double Ee, double Voc0, double NcellSer, double DiodeFactor, double BVoc0, double mBVoc )
{
/*
C Returns Open-Circuit Voltage (V)
C Tc = cell temperature (deg C)
C Ee = effective irradiance
C Voco = Voc at SRC (1000 W/m2, 25 C) (V)
C NcellSer = # cells in series
C DiodeFactor = module-specIFic empirical constant
C BVoc0 = Voc temperature coefficient (V/C)
C mBVoc = change in BVoc with irradiance
*/
	if ( Ee > 0.0 ) 
	{
		double dTc = DiodeFactor*((1.38066E-23*(Tc + 273.15))/1.60218E-19);
		double BVocEe = BVoc0 + mBVoc * (1.0 - Ee);
  
		return Voc0+NcellSer*dTc*log(Ee)+BVocEe*(Tc-25.0);
	}
	else
		return 0.0;
}

static double sandia_vmp( double Tc, double Ee, double Vmp0, double NcellSer, double DiodeFactor, double BVmp0,
	double mBVmp, double C2, double C3 )
{
	/*
C Returns Voltage at Max. Power Point (V)
C Tc = cell temperature (deg C)
C Ee = effective irradiance
C Vmpo = Vmp at SRC (1000 W/m2, 25 C) (V)
C NcellSer = # cells in series
C DiodeFactor = module-specIFic empirical constant
C BVmp0 = Vmp temperature coefficient (V/C)
C mBVmp = change in BVmp with irradiance
C c2,c3 = empirical module-specIFic constants
*/
	if ( Ee > 0.0 )
	{
		double dTc=DiodeFactor*((1.38066E-23*(Tc+273.15))/1.60218E-19);
		double BVmpEe = BVmp0 + mBVmp * (1.0 - Ee);

		return Vmp0+C2*NcellSer*dTc*log(Ee)+C3*NcellSer*pow((dTc*log(Ee)),2)+BVmpEe*(Tc-25.0);
	}
	else
		return 0.0;
}

static double sandia_ixx( double Tc, double Ee, double Ixx0, double aImp, double C6, double C7 )
{
	/*
C Returns current "Ix" at V=0.5*(Voc+Vmp) (A)
C Tc = cell temperature (deg C)
C Ee = effective irradiance
C Ixx0 = Ixx at SRC (1000 W/m2, 25 C) (A)
C aImp = Imp temp coefficient (/C)
C c4,c5 = empirical module-specIFic constants
*/
	return Ixx0*(C6*Ee+C7*Ee*Ee)*(1.0+aImp*(Tc-25.0));
}

static double sandia_ix(double Tc, double Ee, double Ix0, double aIsc, double aImp, double C4, double C5 )
{
	/*
C Returns current "Ix" at V=0.5*Voc (A)
C Tc = cell temperature (deg C)
C Ee = effective irradiance
C Ix0 = Ix at SRC (1000 W/m2, 25 C) (A)
C aIsc = Isc temp coefficient (/C)
C aImp = Imp temp coefficient (/C)
C c4,c5 = empirical module-specIFic constants
*/
	return Ix0*(C4*Ee+C5*Ee*Ee)*(1.0+((aIsc+aImp)/2.0*(Tc-25.0)));
}

static double sandia_isc( double Tc, double Isc0, double Ibc, double Idc, double F1, double F2, double fd, double aIsc )
{
	/*
C Returns Short-Circuit Current
C Isc0 = Isc at Tc=25 C, Ic=1000 W/m2 (A)
C Ibc  = beam radiation on collector plane (W/m2)
C Idc  = Diffuse radiation on collector plane (W/m2)
C F1   = Sandia F1 function of air mass
C F2   = Sandia F2 function of incidence angle
C fd   = module-specIFic empirical constant
C aIsc = Isc temperature coefficient (1/degC)
C Tc   = cell temperature */

// CNB UPDATED 12-3-07 BASED ON INFO FROM UPDATED CODE FROM GREG BARKER
// C       Isc0*((Ibc*F1*F2+fd*Idc)/1000.0)*(1.0+aIsc*(Tc-25.0))
	return Isc0*F1*((Ibc*F2+fd*Idc)/1000.0)*(1.0+aIsc*(Tc-25.0));
}

static double sandia_imp(double Tc, double Ee, double Imp0, double aImp, double C0, double C1)
{ /*
C Returns current at maximum power point (A)
C Tc = cell temperature (degC)
C Ee = effective irradiance (W/m2)
C Imp0 = current at MPP at SRC (1000 W/m2, 25 C) (A)
C aImp = Imp temperature coefficient (degC^-1)
C c0,c1 = empirical module-specIFic constants */
	return Imp0*(C0*Ee+C1*Ee*Ee)*(1.0+aImp*(Tc-25.0));
}

static double sandia_f2( double IncAng, double b0, double b1, double b2, double b3, double b4, double b5 )
{
	/*
C Returns Sandia F2 function
C IncAng = incidence angle (deg)
C b0,b1,b2,b3,b4,b5 = empirical module-specIFic constants
*/

	double F2 = b0
		+ b1*IncAng
		+ b2*IncAng*IncAng
		+ b3*IncAng*IncAng*IncAng
		+ b4*IncAng*IncAng*IncAng*IncAng
		+ b5*IncAng*IncAng*IncAng*IncAng*IncAng;
  
	return F2 > 0.0 ? F2 : 0.0;
} 

static double sandia_f1( double AMa, double a0, double a1, double a2, double a3, double a4 )
{
	/*
C Returns the Sandia Air Mass function
C AMa = absolute air mass
C a0,a1,a2,a3,a4 = empirical constants, module-specIFic
*/
  
	double F1 = a0 
		+ a1*AMa 
		+ a2*AMa*AMa 
		+ a3*AMa*AMa*AMa 
		+ a4*AMa*AMa*AMa*AMa;

	return F1 > 0.0 ? F1 : 0.0;
} 

static double sandia_absolute_air_mass( double SolZen, double Altitude )
{
 /*
C Returns absolute air mass
C SolZen = solar zenith (deg)
C Altitude = site altitude (m)
*/
	if (SolZen < 89.9)
	{
		double AM = 1/(cos(SolZen * 0.01745329) + 0.5057 * pow( (96.08 - SolZen),-1.634));
		return AM * exp(-0.0001184 * Altitude);
	}
	else
		return 999;
}

static double sandia_effective_irradiance( double Tc, double Isc, double Isc0, double aIsc )
{
	/*
C Returns "effective irradiance", used in calculation of Imp, Voc, Ix, Ixx
C Tc   = cell temperature
C Isc = short-circuit current under operating conditions (A)
C Isc0 = Isc at Tc=25 C, Ic=1000 W/m2 (A)
C aIsc = Isc temperature coefficient (degC^-1) */
	return Isc / (1.0+aIsc*(Tc - 25.0))/Isc0;
}

static double sandia_cell_temperature( double Ibc, double Idc, double Ws, double Ta, double fd, double a, double b, double DT0 )
{
/*
C Returns cell temperature, deg C
C Ibc = beam radiation on collector plane, W/m2
C Idc = Diffuse radiation on collector plane, W/m2
C Ws  = wind speed, m/s
C Ta  = ambient temperature, degC
C fd  = fraction of Idc used (empirical constant)
C a   = empirical constant
C b   = empirical constant
C DT0 = (Tc-Tm) at E=1000 W/m2 (empirical constant known as dTc), deg C
*/
  
//C Update from Chris Cameron - email 4/28/10  
//C        E = Ibc + fd * Idc
	double E = Ibc + Idc;  
	double Tm = E * exp(a + b * Ws) + Ta;  
	return Tm + E / 1000.0 * DT0;
}

static double sandia_tcell_from_tmodule( double Tm, double Ibc, double Idc, double fd, double DT0)
{
	/*
C Returns cell temperature, deg C
C Tm  = module temperature (deg C)
C Ibc = beam radiation on collector plane, W/m2
C Idc = Diffuse radiation on collector plane, W/m2
C fd  = fraction of Idc used (empirical constant)
C DT0 = (Tc-Tm) at E=1000 W/m2 (empirical constant known as dTc), deg C
*/

//C Update from Chris Cameron - email 4/28/10  
//C        E = Ibc + fd * Idc
	double E = Ibc + Idc;
	return Tm + E / 1000.0 * DT0;
}

static double sandia_module_temperature( double Ibc, double Idc, double Ws, double Ta, double fd, double a, double b )
{
	/*
C Returns back-of-module temperature, deg C
C Ibc = beam radiation on collector plane, W/m2
C Idc = Diffuse radiation on collector plane, W/m2
C Ws  = wind speed, m/s
C Ta  = ambient temperature, degC
C fd  = fraction of Idc used (empirical constant)
C a   = empirical constant
C b   = empirical constant
*/

//C Update from Chris Cameron - email 4/28/10  
//C        E = Ibc + fd * Idc
	double E = Ibc + Idc;
	return E * exp(a + b * Ws) + Ta;
}



void sandia_module(
	/* parameters */
	sandia_module_t *pModule,

	/* inputs */
	double PoaBeam,
	double PoaDiffuse,
	double ZenithAngle,
	double IncidenceAngle,
	double Tamb,
	double WindSpeed,
	double Altitude,
	
	/* outputs */
	double *Pmp, double *Vmp, double *Imp,
	double *Eff,
	double *Tcell, double *Tback,
	double *Vx, double *Ix,
	double *Vxx, double *Ixx,
	double *Voc, double *Isc,
	double *AMa, double *F1, double *F2
	)
{
	sandia_module_t &m = *pModule;

	*Pmp = 0.0;
	*Vmp = 0.0;
	*Imp = 0.0;
	*Eff = 0.0;
	*Vx = 0.0;
	*Ix = 0.0;
	*Vxx = 0.0;
	*Ixx = 0.0;
	*Voc = 0.0;
	*Isc = 0.0;
	*AMa = 0.0;
	*F1 = 0.0;
	*F2 = 0.0;
	
	//C Calculate back-of-module temperature:
	*Tback = sandia_module_temperature(PoaBeam,PoaDiffuse,WindSpeed,Tamb,m.fd,m.a,m.b);

	//C Calculate cell temperature:
	*Tcell = sandia_tcell_from_tmodule(*Tback,PoaBeam,PoaDiffuse,m.fd,m.DT0);

	if ( PoaBeam+PoaDiffuse > 0.0 )
	{
		//C Calculate Air Mass
		*AMa = sandia_absolute_air_mass(ZenithAngle, Altitude);

		//C Calculate F1 function:
		*F1 = sandia_f1(*AMa,m.A0,m.A1,m.A2,m.A3,m.A4);

		//C Calculate F2 function:
		*F2 = sandia_f2(IncidenceAngle,m.B0,m.B1,m.B2,m.B3,m.B4,m.B5);

		//C Calculate short-circuit current:
		*Isc = sandia_isc(*Tcell,m.Isc0,PoaBeam,PoaDiffuse,*F1,*F2,m.fd,m.aIsc);

		//C Calculate effective irradiance:
		double Ee = sandia_effective_irradiance(*Tcell,*Isc,m.Isc0,m.aIsc);

		//C Calculate Imp:
		*Imp = sandia_imp(*Tcell,Ee,m.Imp0,m.aImp,m.C0,m.C1);

		//C Calculate Voc:
		*Voc = sandia_voc(*Tcell,Ee,m.Voc0,m.NcellSer,m.DiodeFactor,m.BVoc0,m.mBVoc);

		//C Calculate Vmp:
		*Vmp = sandia_vmp(*Tcell,Ee,m.Vmp0,m.NcellSer,m.DiodeFactor,m.BVmp0,m.mBVmp,m.C2,m.C3);

		//C Calculate Ix:
		*Ix = sandia_ix(*Tcell,Ee,m.Ix0,m.aIsc,m.aImp,m.C4,m.C5);

		//C Calculate Vx:
		*Vx = *Voc/2.0;

		//C Calculate Ixx:
		*Ixx = sandia_ixx(*Tcell,Ee,m.Ixx0,m.aImp,m.C6,m.C7);

		//C Calculate Vxx:
		*Vxx = 0.5*(*Voc + *Vmp);

		//C Calculate Pmp, single module:
		*Pmp = *Imp * *Vmp;

		//C Calculate max efficiency:
		*Eff = *Pmp/((PoaBeam+PoaDiffuse)*m.Area);
	}
}

void sandia_inverter(	
	/* parameters */
	sandia_inverter_t *pInverter,

	/* inputs */
	double Pdc,     /* Input power to inverter (Wdc) */
	double Vdc,     /* Voltage input to inverter (Vdc) */

	/* outputs */
	double *Pac,    /* AC output power (Wac) */
	double *Ppar,   /* AC parasitic power consumption (Wac) */
	double *Plr,    /* Part load ratio (Pdc_in/Pdc_rated, 0..1) */
	double *Eff	    /* Conversion efficiency (0..1) */
	)
{
	sandia_inverter_t &i = *pInverter;

	double A = i.Pdco * ( 1.0 + i.C1*( Vdc - i.Vdco ));
	double B = i.Pso * ( 1.0 + i.C2*( Vdc - i.Vdco ));
	double C = i.C0 * ( 1.0 + i.C3*( Vdc - i.Vdco ));

	*Pac = ((i.Paco / (A-B)) - C*(A-B))*(Pdc-B) + i.C0*(Pdc-B)*(Pdc-B);
	*Ppar = 0.0;

	if (Pdc <= i.Pso)
	{
		*Pac = -i.Pntare;
		*Ppar = i.Pntare;
	}

	if ( *Pac > i.Paco ) *Pac = i.Paco;

	*Plr = Pdc / i.Pdco;
	*Eff = *Pac / Pdc;
	if ( *Eff < 0.0 ) *Eff = 0.0;
}
