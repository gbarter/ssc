#include "sco2_pc_core.h"
#include "CO2_properties.h"
#include <limits>
#include <algorithm>

#include "nlopt.hpp"
#include "nlopt_callbacks.h"

#include "fmin.h"

using namespace std;


const double C_turbine::m_nu_design = 0.7476;
const double C_compressor::m_snl_phi_design = 0.02971;		//[-] Design-point flow coef. for Sandia compressor (corresponds to max eta)
const double C_compressor::m_snl_phi_min = 0.02;				//[-] Approximate surge limit for SNL compressor
const double C_compressor::m_snl_phi_max = 0.05;				//[-] Approximate x-intercept for SNL compressor
const double C_recompressor::m_snl_phi_design = 0.02971;		//[-] Design-point flow coef. for Sandia compressor (corresponds to max eta)
const double C_recompressor::m_snl_phi_min = 0.02;				//[-] Approximate surge limit for SNL compressor
const double C_recompressor::m_snl_phi_max = 0.05;				//[-] Approximate x-intercept for SNL compressor

void calculate_turbomachinery_outlet_1(double T_in /*K*/, double P_in /*kPa*/, double P_out /*kPa*/, double eta /*-*/, bool is_comp, int & error_code, double & spec_work /*kJ/kg*/)
{
	double enth_in, entr_in, dens_in, temp_out, enth_out, entr_out, dens_out;

	calculate_turbomachinery_outlet_1(T_in, P_in, P_out, eta, is_comp, error_code, enth_in, entr_in, dens_in, temp_out, enth_out, entr_out, dens_out, spec_work);
}

void calculate_turbomachinery_outlet_1(double T_in /*K*/, double P_in /*kPa*/, double P_out /*kPa*/, double eta /*-*/, bool is_comp, int & error_code, double & enth_in /*kJ/kg*/, double & entr_in /*kJ/kg-K*/,
	double & dens_in /*kg/m3*/, double & temp_out /*K*/, double & enth_out /*kJ/kg*/, double & entr_out /*kJ/kg-K*/, double & dens_out /*kg/m3*/, double & spec_work /*kJ/kg*/)
{
	/*Calculates the outlet state of a compressor or turbine using its isentropic efficiency.
	is_comp = .true.means the turbomachine is a compressor(w = w_s / eta)
	is_comp = .false.means the turbomachine is a turbine(w = w_s * eta) */

	CO2_state co2_props;

	error_code = 0;

	int prop_error_code = CO2_TP(T_in, P_in, &co2_props);		// properties at the inlet conditions
	if( prop_error_code != 0 )
	{
		error_code = prop_error_code;
		return;
	}
	double h_in = co2_props.enth;
	double s_in = co2_props.entr;
	dens_in = co2_props.dens;

	prop_error_code = CO2_PS(P_out, s_in, &co2_props);			// outlet enthalpy if compression/expansion is isentropic
	if( prop_error_code != 0 )
	{
		error_code = prop_error_code;
		return;
	}
	double h_s_out = co2_props.enth;

	double w_s = h_in - h_s_out;			// specific work if process is isentropic (negative for compression, positive for expansion)

	double w = 0.0;
	if( is_comp )
		w = w_s / eta;						// actual specific work of compressor (negative)
	else
		w = w_s * eta;						// actual specific work of turbine (positive)

	double h_out = h_in - w;

	prop_error_code = CO2_PH(P_out, h_out, &co2_props);
	if( prop_error_code != 0 )
	{
		error_code = prop_error_code;
		return;
	}

	enth_in = h_in;
	entr_in = s_in;
	temp_out = co2_props.temp;
	enth_out = h_out;
	entr_out = co2_props.entr;
	dens_out = co2_props.dens;
	spec_work = w;

	return;
};

void calculate_hxr_UA_1(int N_hxrs, double Q_dot /*units?*/, double m_dot_c, double m_dot_h, double T_c_in, double T_h_in, double P_c_in, double P_c_out, double P_h_in, double P_h_out,
	int & error_code, double & UA, double & min_DT)
{
	/*Calculates the UA of a heat exchanger given its mass flow rates, inlet temperatures, and a heat transfer rate.
	Note: the heat transfer rate must be positive.*/

	// Check inputs
	if( Q_dot < 0.0 )
	{
		error_code = 4;
		return;
	}
	if( T_h_in < T_c_in )
	{
		error_code = 5;
		return;
	}
	if( P_h_in < P_h_out )
	{
		error_code = 6;
		return;
	}
	if( P_c_in < P_c_out )
	{
		error_code = 7;
		return;
	}
	if( Q_dot <= 1.E-14 )		// very low Q_dot; assume it is zero
	{
		UA = 0.0;
		min_DT = T_h_in - T_c_in;
		return;
	}

	// Calculate inlet enthalpies from known state points
	CO2_state co2_props;
	int prop_error_code = CO2_TP(T_c_in, P_c_in, &co2_props);
	if( prop_error_code != 0 )
	{
		error_code = prop_error_code;
		return;
	}
	double h_c_in = co2_props.enth;

	prop_error_code = CO2_TP(T_h_in, P_h_in, &co2_props);
	if( prop_error_code != 0 )
	{
		error_code = 9;
		return;
	}
	double h_h_in = co2_props.enth;

	// Calculate outlet enthalpies from energy balance
	double h_c_out = h_c_in + Q_dot / m_dot_c;
	double h_h_out = h_h_in - Q_dot / m_dot_h;

	int N_nodes = N_hxrs + 1;
	double h_h_prev = 0.0;
	double T_h_prev = 0.0;
	double h_c_prev = 0.0;
	double T_c_prev = 0.0;
	UA = 0.0;
	min_DT = T_h_in;
	// Loop through the sub-heat exchangers
	for( int i = 0; i < N_nodes; i++ )
	{
		// Assume pressure varies linearly through heat exchanger
		double P_c = P_c_out + i*(P_c_in - P_c_out) / (N_nodes - 1);
		double P_h = P_h_in - i*(P_h_in - P_h_out) / (N_nodes - 1);

		// Calculate the entahlpy at the node
		double h_c = h_c_out + i*(h_c_in - h_c_out) / (N_nodes - 1);
		double h_h = h_h_in - i*(h_h_in - h_h_out) / (N_nodes - 1);

		// Calculate the hot and cold temperatures at the node
		prop_error_code = CO2_PH(P_h, h_h, &co2_props);
		if( prop_error_code != 0 )
		{
			error_code = 12;
			return;
		}
		double T_h = co2_props.temp;

		prop_error_code = CO2_PH(P_c, h_c, &co2_props);
		if( prop_error_code != 0 )
		{
			error_code = 13;
			return;
		}
		double T_c = co2_props.temp;

		// Check that 2nd law was not violated
		if( T_c >= T_h )
		{
			error_code = 11;
			return;
		}

		// Track the minimum temperature difference in the heat exchanger
		min_DT = min(min_DT, T_h - T_c);

		// Perform effectiveness-NTU and UA calculations 
		if( i > 0 )
		{
			double C_dot_h = m_dot_h*(h_h_prev - h_h) / (T_h_prev - T_h);			// [kW/K] hot stream capacitance rate
			double C_dot_c = m_dot_c*(h_c_prev - h_c) / (T_c_prev - T_c);			// [kW/K] cold stream capacitance rate
			double C_dot_min = min(C_dot_h, C_dot_c);				// [kW/K] Minimum capacitance stream
			double C_dot_max = max(C_dot_h, C_dot_c);				// [kW/K] Maximum capacitance stream
			double C_R = C_dot_min / C_dot_max;						// [-] Capacitance ratio of sub-heat exchanger
			double eff = (Q_dot / (double)N_hxrs) / (C_dot_min*(T_h_prev - T_c));	// [-] Effectiveness of each sub-heat exchanger
			double NTU = 0.0;
			if( C_R != 1.0 )
				NTU = log((1.0 - eff*C_R) / (1.0 - eff)) / (1.0 - C_R);		// [-] NTU if C_R does not equal 1
			else
				NTU = eff / (1.0 - eff);
			UA += NTU*C_dot_min;						// [kW/K] Sum UAs for each hx section			
		}
		h_h_prev = h_h;
		T_h_prev = T_h;
		h_c_prev = h_c;
		T_c_prev = T_c;
	}

	// Check for NaNs that arose
	if( UA != UA )
	{
		error_code = 14;
		return;
	}

	return;
};

void isen_eta_from_poly_eta(double T_in /*K*/, double P_in /*kPa*/, double P_out /*kPa*/, double poly_eta /*-*/, bool is_comp, int & error_code, double & isen_eta)
{
	/* 9.3.14: code written by John Dyreby, translated to C++ by Ty Neises
	! Calculate the isentropic efficiency that corresponds to a given polytropic efficiency
	! for the expansion or compression from T_in and P_in to P_out.
	!
	! Inputs:
	!   T_in -- inlet temperature (K)
	!   P_in -- inlet pressure (kPa)
	!   P_out -- outlet pressure (kPa)
	!   poly_eta -- polytropic efficiency (-)
	!   is_comp -- if .true., model a compressor (w = w_s / eta); if .false., model a turbine (w = w_s * eta)
	!
	! Outputs:
	!   error_trace -- an ErrorTrace object
	!   isen_eta -- the equivalent isentropic efficiency (-)
	!
	! Notes:
	!   1) Integration of small DP is approximated numerically by using 200 stages.
	!   2) No error checking is performed on the inlet and outlet pressures; valid pressure ratios are assumed. */

	CO2_state co2_props;
	
	// Properties at the inlet conditions
	int prop_error_code = CO2_TP(T_in, P_in, &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	double h_in = co2_props.enth;
	double s_in = co2_props.entr;

	// Outlet enthalpy if compression/expansion is isentropic
	prop_error_code = CO2_PS(P_out, s_in, &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	double h_s_out = co2_props.enth;

	double stage_P_in = P_in;		// Initialize first stage inlet pressure
	double stage_h_in = h_in;		// Initialize first stage inlet enthalpy
	double stage_s_in = s_in;		// Initialize first stage inlet entropy

	int N_stages = 200;

	double stage_DP = (P_out - P_in) / (double)N_stages;

	double stage_P_out = -999.9;
	double stage_h_out = -999.9;

	for( int i = 1; i <= N_stages; i++ )
	{
		stage_P_out = stage_P_in + stage_DP;

		// Outlet enthalpy if compression/expansion is isentropic
		prop_error_code = CO2_PS(stage_P_out, stage_s_in, &co2_props);
		if( prop_error_code != 0 )
		{
			error_code = prop_error_code;
			return;
		}
		double stage_h_s_out = co2_props.enth;

		double w_s = stage_h_in - stage_h_s_out;		// specific work if process is isentropic
		double w = numeric_limits<double>::quiet_NaN();
		if( is_comp )
			w = w_s / poly_eta;
		else
			w = w_s * poly_eta;
		stage_h_out = stage_h_in - w;

		// Reset next stage inlet values
		stage_P_in = stage_P_out;
		stage_h_in = stage_h_out;

		prop_error_code = CO2_PH(stage_P_in, stage_h_in, &co2_props);
		if( prop_error_code != 0 )
		{
			error_code = prop_error_code;
			return;
		}
		stage_s_in = co2_props.entr;
	}

	// Note: last stage outlet enthalpy is equivalent to turbomachinery outlet enthalpy
	if( is_comp )
		isen_eta = (h_s_out - h_in) / (stage_h_out - h_in);
	else
		isen_eta = (stage_h_out - h_in) / (h_s_out - h_in);
}

void C_RecompCycle::design_core(int & error_code)
{
	CO2_state co2_props;

	int max_iter = 500;
	double temperature_tolerance = 1.E-6;		// Temp differences below this are considered zero

	int cpp_offset = 1;

	// Initialize a few variables
	double m_dot_t, m_dot_mc, m_dot_rc, Q_dot_LT, Q_dot_HT, UA_LT_calc, UA_HT_calc;
	m_dot_t = m_dot_mc = m_dot_rc = Q_dot_LT = Q_dot_HT = UA_LT_calc = UA_HT_calc = 0.0;

	m_temp_last[1 - cpp_offset] = ms_des_par.m_T_mc_in;
	m_pres_last[1 - cpp_offset] = ms_des_par.m_P_mc_in;
	m_pres_last[2 - cpp_offset] = ms_des_par.m_P_mc_out;
	m_temp_last[6 - cpp_offset] = ms_des_par.m_T_t_in;

	// Apply pressure drops to heat exchangers, fully defining the pressures at all states
	if(ms_des_par.m_DP_LT[1-cpp_offset] < 0.0)
		m_pres_last[3-cpp_offset] = m_pres_last[2-cpp_offset] - m_pres_last[2-cpp_offset]*fabs(ms_des_par.m_DP_LT[1-cpp_offset]);	// relative pressure drop specified for LT recuperator (cold stream)
	else
		m_pres_last[3-cpp_offset] = m_pres_last[2-cpp_offset] - ms_des_par.m_DP_LT[1-cpp_offset];									// absolute pressure drop specified for LT recuperator (cold stream)

	if(ms_des_par.m_UA_LT < 1.0E-12)
		m_pres_last[3-cpp_offset] = m_pres_last[2-cpp_offset];		// If there is no LT recuperator, there is no pressure drop

	m_pres_last[4-cpp_offset] = m_pres_last[3-cpp_offset];			// Assume no pressure drop in mixing valve
	m_pres_last[10-cpp_offset] = m_pres_last[3-cpp_offset];			// Assume no pressure drop in mixing valve

	if(ms_des_par.m_DP_HT[1-cpp_offset] < 0.0)
		m_pres_last[5-cpp_offset] = m_pres_last[4-cpp_offset] - m_pres_last[4-cpp_offset]*fabs(ms_des_par.m_DP_HT[1-cpp_offset]);	// relative pressure drop specified for HT recuperator (cold stream)
	else
		m_pres_last[5-cpp_offset] = m_pres_last[4-cpp_offset] - ms_des_par.m_DP_HT[1-cpp_offset];									// absolute pressure drop specified for HT recuperator (cold stream)

	if(ms_des_par.m_UA_HT < 1.0E-12)
		m_pres_last[5-cpp_offset] = m_pres_last[4-cpp_offset];		// If there is no HT recuperator, there is no pressure drop

	if(ms_des_par.m_DP_PHX[1-cpp_offset] < 0.0)
		m_pres_last[6-cpp_offset] = m_pres_last[5-cpp_offset] - m_pres_last[5-cpp_offset]*fabs(ms_des_par.m_DP_PHX[1-cpp_offset]);	// relative pressure drop specified for PHX
	else
		m_pres_last[6-cpp_offset] = m_pres_last[5-cpp_offset] - ms_des_par.m_DP_PHX[1-cpp_offset];									// absolute pressure drop specified for PHX

	if(ms_des_par.m_DP_PC[2-cpp_offset] < 0.0)
		m_pres_last[9-cpp_offset] = m_pres_last[1-cpp_offset]/(1.0 - fabs(ms_des_par.m_DP_PC[2-cpp_offset]));	// relative pressure drop specified for precooler: P1=P9-P9*rel_DP => P1=P9*(1-rel_DP)
	else
		m_pres_last[9-cpp_offset] = m_pres_last[1-cpp_offset] + ms_des_par.m_DP_PC[2-cpp_offset];

	if(ms_des_par.m_DP_LT[2-cpp_offset] < 0.0)
		m_pres_last[8-cpp_offset] = m_pres_last[9-cpp_offset]/(1.0 - fabs(ms_des_par.m_DP_LT[2-cpp_offset]));	// relative pressure drop specified for LT recuperator (hot stream)
	else
		m_pres_last[8-cpp_offset] = m_pres_last[9-cpp_offset] + ms_des_par.m_DP_LT[2-cpp_offset];				// absolute pressure drop specified for LT recuperator (hot stream)

	if(ms_des_par.m_UA_LT < 1.0E-12)
		m_pres_last[8-cpp_offset] = m_pres_last[9-cpp_offset];		// if there is no LT recuperator, there is no pressure drop

	if(ms_des_par.m_DP_HT[2-cpp_offset] < 0.0)
		m_pres_last[7-cpp_offset] = m_pres_last[8-cpp_offset]/(1.0 - fabs(ms_des_par.m_DP_HT[2-cpp_offset]));	// relative pressure drop specified for HT recuperator (hot stream)
	else
		m_pres_last[7-cpp_offset] = m_pres_last[8-cpp_offset] + ms_des_par.m_DP_HT[2-cpp_offset];				// absolute pressure drop specified for HT recuperator (hot stream)

	if(ms_des_par.m_UA_HT < 1.0E-12)
		m_pres_last[7-cpp_offset] = m_pres_last[8-cpp_offset];		// if there is no HT recuperator, there is no pressure drop

	// Determine equivalent isentropic efficiencies for main compressor and turbine, if necessary.
	double eta_mc_isen = std::numeric_limits<double>::quiet_NaN();
	double eta_t_isen = std::numeric_limits<double>::quiet_NaN();
	if(ms_des_par.m_eta_mc < 0.0)
	{
		int poly_error_code = 0;
		
		isen_eta_from_poly_eta(m_temp_last[1-cpp_offset], m_pres_last[1-cpp_offset], m_pres_last[2-cpp_offset], fabs(ms_des_par.m_eta_mc),
			true, poly_error_code, eta_mc_isen);

		if(poly_error_code != 0)
		{
			error_code = poly_error_code;
			return;
		}
	}
	else
		eta_mc_isen = ms_des_par.m_eta_mc;

	if(ms_des_par.m_eta_t < 0.0)
	{
		int poly_error_code = 0;

		isen_eta_from_poly_eta(m_temp_last[6-cpp_offset], m_pres_last[6-cpp_offset], m_pres_last[7-cpp_offset], fabs(ms_des_par.m_eta_t),
			false, poly_error_code, eta_t_isen);
	
		if(poly_error_code != 0)
		{
			error_code = poly_error_code;
			return;
		}
	}
	else
		eta_t_isen = ms_des_par.m_eta_t;

	// Determine the outlet state and specific work for the main compressor and turbine.
	int comp_error_code = 0;
	double w_mc = std::numeric_limits<double>::quiet_NaN();
		// Main compressor
	calculate_turbomachinery_outlet_1(m_temp_last[1-cpp_offset], m_pres_last[1-cpp_offset], m_pres_last[2-cpp_offset], eta_mc_isen, true,
		comp_error_code, m_enth_last[1-cpp_offset], m_entr_last[1-cpp_offset], m_dens_last[1-cpp_offset], m_temp_last[2-cpp_offset],
		m_enth_last[2-cpp_offset], m_entr_last[2-cpp_offset], m_dens_last[2-cpp_offset], w_mc);

	if(comp_error_code != 0)
	{
		error_code = comp_error_code;
		return;
	}

	int turbine_error_code = 0;
	double w_t = std::numeric_limits<double>::quiet_NaN();
		// Turbine
	calculate_turbomachinery_outlet_1(m_temp_last[6-cpp_offset], m_pres_last[6-cpp_offset], m_pres_last[7-cpp_offset], eta_t_isen, false,
		turbine_error_code, m_enth_last[6-cpp_offset], m_entr_last[6-cpp_offset], m_dens_last[6-cpp_offset], m_temp_last[7-cpp_offset],
		m_enth_last[7-cpp_offset], m_entr_last[7-cpp_offset], m_dens_last[7-cpp_offset], w_t);

	if(turbine_error_code != 0)
	{
		error_code = turbine_error_code;
		return;
	}

	// Check that this cycle can produce power
	double eta_rc_isen = std::numeric_limits<double>::quiet_NaN();
	double w_rc = std::numeric_limits<double>::quiet_NaN();
	if(ms_des_par.m_recomp_frac >= 1.E-12)
	{
		if(ms_des_par.m_eta_rc < 0.0)		// need to convert polytropic efficiency to isentropic efficiency
		{
			int rc_error_code = 0;

			isen_eta_from_poly_eta(m_temp_last[2-cpp_offset], m_pres_last[9-cpp_offset], m_pres_last[10-cpp_offset], fabs(ms_des_par.m_eta_rc),
				true, rc_error_code, eta_rc_isen);
		
			if(rc_error_code != 0)
			{
				error_code = rc_error_code;
				return;
			}
		}
		else
			eta_rc_isen = ms_des_par.m_eta_rc;

		int rc_error_code = 0;
		calculate_turbomachinery_outlet_1(m_temp_last[2-cpp_offset], m_pres_last[9-cpp_offset], m_pres_last[10-cpp_offset], eta_rc_isen,
			true, rc_error_code, w_rc);

		if(rc_error_code != 0)
		{
			error_code = rc_error_code;
			return;
		}
	}
	else
		w_rc = 0.0;

	if(w_mc + w_rc + w_t <= 0.0)	// positive net power is impossible; return an error
	{
		error_code = 25;
		return;
	}

	// Outer iteration loop: temp(8), checking against UA_HT
	double T8_lower_bound, T8_upper_bound, last_HT_residual, last_T8_guess;
	T8_lower_bound = T8_upper_bound = last_HT_residual = last_T8_guess = std::numeric_limits<double>::quiet_NaN();
	if(ms_des_par.m_UA_HT < 1.0E-12)		// no high-temp recuperator
	{
		T8_lower_bound = m_temp_last[7-cpp_offset];		// no iteration necessary
		T8_upper_bound = m_temp_last[7-cpp_offset];		// no iteration necessary
		m_temp_last[8-cpp_offset] = m_temp_last[7-cpp_offset];
		UA_HT_calc = 0.0;
		last_HT_residual = 0.0;
		last_T8_guess = m_temp_last[7-cpp_offset];
	}
	else
	{
		T8_lower_bound = m_temp_last[2-cpp_offset];		// the absolute lower temp(8) could be
		T8_upper_bound = m_temp_last[7-cpp_offset];		// the absolutely highest temp(8) could be
		m_temp_last[8-cpp_offset] = (T8_lower_bound + T8_upper_bound)*0.5;		// bisect bounds for first guess
		UA_HT_calc = -1.0;
		last_HT_residual = ms_des_par.m_UA_HT;			// know a priori that with T8 = T7, UA_calc = 0 therefore residual is UA_HT - 0.0
		last_T8_guess = m_temp_last[7-cpp_offset];
	}

	int prop_error_code = 0;

	double T9_lower_bound, T9_upper_bound, last_LT_residual, last_T9_guess;
	T9_lower_bound = T9_upper_bound = last_LT_residual = last_T9_guess = std::numeric_limits<double>::quiet_NaN();

	double min_DT_LT = std::numeric_limits<double>::quiet_NaN();
	double min_DT_HT = std::numeric_limits<double>::quiet_NaN();

	int T8_iter = -1;
	for( T8_iter = 0; T8_iter < max_iter; T8_iter++ )
	{
		// Fully define state 8
		prop_error_code = CO2_TP(m_temp_last[8-cpp_offset], m_pres_last[8-cpp_offset], &co2_props);
		if(prop_error_code != 0)
		{
			error_code = prop_error_code;
			return;
		}
		m_enth_last[8-cpp_offset] = co2_props.enth;
		m_entr_last[8-cpp_offset] = co2_props.entr;
		m_dens_last[8-cpp_offset] = co2_props.dens;
	
		// Inner iteration loop: temp(9), checking against UA_LT
		if(ms_des_par.m_UA_LT < 1.0E-12)	// no low-temperature recuperator
		{
			T9_lower_bound = m_temp_last[8-cpp_offset];		// no iteration necessary
			T9_upper_bound = m_temp_last[8-cpp_offset];		// no iteration necessary
			m_temp_last[9-cpp_offset] = m_temp_last[8-cpp_offset];
			UA_LT_calc = 0.0;
			last_LT_residual = 0.0;
			last_T9_guess = m_temp_last[8-cpp_offset];		
		}
		else
		{
			T9_lower_bound = m_temp_last[2-cpp_offset];		// the absolute lowest temp(9) could be
			T9_upper_bound = m_temp_last[8-cpp_offset];		// the absolute highest temp(9) could be
			m_temp_last[9-cpp_offset] = (T9_lower_bound + T9_upper_bound)*0.5;	// biset bounds for first guess
			UA_LT_calc = -1.0;
			last_LT_residual = ms_des_par.m_UA_LT;			// know a priori that with T9 = T8, UA_calc = 0 therefore residual is UA_LT - 0
			last_T9_guess = m_temp_last[8-cpp_offset];
		}
	
		int T9_iter = -1;
		for( T9_iter = 0; T9_iter < max_iter; T9_iter++ )
		{
			// Determine the outlet state of the recompressing compressor and its specific work
			if(ms_des_par.m_recomp_frac >= 1.E-12)
			{
				if(ms_des_par.m_eta_rc < 0.0)		// recalculate isentropic efficiency of recompressing compressor (because T9 changes)
				{
					int rc_error_code = 0;
					isen_eta_from_poly_eta(m_temp_last[9-cpp_offset], m_pres_last[9-cpp_offset], m_pres_last[10-cpp_offset], fabs(ms_des_par.m_eta_rc),true,
						rc_error_code, eta_rc_isen);
				
					if(rc_error_code != 0)
					{
						error_code = rc_error_code;
						return;
					}
				}
				else
				{
					eta_rc_isen = ms_des_par.m_eta_rc;
				}			
			
				int rc_error_code = 0;
				calculate_turbomachinery_outlet_1(m_temp_last[9-cpp_offset], m_pres_last[9-cpp_offset], m_pres_last[10-cpp_offset], eta_rc_isen, true, rc_error_code,
					m_enth_last[9-cpp_offset], m_entr_last[9-cpp_offset], m_dens_last[9-cpp_offset], m_temp_last[10-cpp_offset], m_enth_last[10-cpp_offset], m_entr_last[10-cpp_offset],
					m_dens_last[10-cpp_offset], w_rc);

				if(rc_error_code != 0)
				{
					error_code = rc_error_code;
					return;
				}
			}
			else
			{
				w_rc = 0.0;		// the recompressing compressor does not exist
				prop_error_code = CO2_TP(m_temp_last[9-cpp_offset], m_pres_last[9-cpp_offset], &co2_props);
				if(prop_error_code != 0)		// fully define state 9
				{
					error_code = prop_error_code;
					return;
				}
				m_enth_last[9-cpp_offset] = co2_props.enth;
				m_entr_last[9-cpp_offset] = co2_props.entr;
				m_dens_last[9-cpp_offset] = co2_props.dens;
				m_temp_last[10-cpp_offset] = m_temp_last[9-cpp_offset];		// assume state 10 is the same as state 9
				m_enth_last[10-cpp_offset] = m_enth_last[9-cpp_offset];
				m_entr_last[10-cpp_offset] = m_entr_last[9-cpp_offset];
				m_dens_last[10-cpp_offset] = m_dens_last[9-cpp_offset];
			}

			// Knowing the specific work of the recompressor, the required mass flow rate can be calculated
			m_dot_t = ms_des_par.m_W_dot_net/(w_mc*(1.0-ms_des_par.m_recomp_frac) + w_rc*ms_des_par.m_recomp_frac + w_t);	// Required mass flow rate through turbine
			if(m_dot_t < 0.0)		// positive power output is not possible with these inputs
			{
				error_code = 29;
				return;
			}
			m_dot_rc = m_dot_t * ms_des_par.m_recomp_frac;		// apply definition of recompression fraction
			m_dot_mc = m_dot_t - m_dot_rc;						// mass balance

			// Calculate the UA value of the low-temperature recuperator
			if( ms_des_par.m_UA_LT < 1.0E-12 )		// no low-temp recuperator (this check is necessary to prevent pressure drops with UA=0 from causing problems)
				Q_dot_LT = 0.0;
			else
				Q_dot_LT = m_dot_t * (m_enth_last[8-cpp_offset] - m_enth_last[9-cpp_offset]);

			int hx_error_code = 0;
			min_DT_LT = std::numeric_limits<double>::quiet_NaN();
			calculate_hxr_UA_1(ms_des_par.m_N_sub_hxrs,Q_dot_LT,m_dot_mc,m_dot_t,m_temp_last[2-cpp_offset],m_temp_last[8-cpp_offset],
				m_pres_last[2-cpp_offset],m_pres_last[3-cpp_offset],m_pres_last[8-cpp_offset],m_pres_last[9-cpp_offset],
				hx_error_code, UA_LT_calc, min_DT_LT);

			if(hx_error_code != 0)
			{
				if(hx_error_code == 11)		// second-law violation in hxr, therefore temp(9) is too low
				{
					T9_lower_bound = m_temp_last[9-cpp_offset];
					m_temp_last[9-cpp_offset] = 0.5*(T9_lower_bound + T9_upper_bound);		// bisect bounds for next guess
					continue;				
				}
				else
				{
					error_code = hx_error_code;
					return;
				}
			}

			// Check for convergence and adjust T9 appropriately
			double UA_LT_residual = ms_des_par.m_UA_LT - UA_LT_calc;

			if( fabs(UA_LT_residual) < 1.0E-12 )		// catches no LT case
				break;

			double secant_guess = m_temp_last[9-cpp_offset] - UA_LT_residual*(last_T9_guess-m_temp_last[9-cpp_offset])/(last_LT_residual-UA_LT_residual);	// next guess predicted using secant method

			if(UA_LT_residual < 0.0)		// UA_LT_calc is too big, temp(9) needs to be higher
			{
				if( fabs(UA_LT_residual) / ms_des_par.m_UA_LT < ms_des_par.m_tol )
					break;

				T9_lower_bound = m_temp_last[9-cpp_offset];
			}
			else		// UA_LT_calc is too small, temp(9) needs to be lower
			{
				if( UA_LT_residual / ms_des_par.m_UA_LT < ms_des_par.m_tol )	// UA_LT converged
					break;

				if( min_DT_LT < temperature_tolerance )		// UA_calc is still too low but there isn't anywhere to go so it's ok (catches huge UA values)
					break;

				T9_upper_bound = m_temp_last[9-cpp_offset];
			}

			last_LT_residual = UA_LT_residual;			// reset lsat stored residual value
			last_T9_guess = m_temp_last[9-cpp_offset];	// reset last stored guess value

			// Check if the secant method overshoots and fall back to bisection if it does
			if(secant_guess <= T9_lower_bound || secant_guess >= T9_upper_bound || secant_guess != secant_guess)	// secant method overshot (or is NaN), use bisection
				m_temp_last[9-cpp_offset] = 0.5*(T9_lower_bound + T9_upper_bound);
			else
				m_temp_last[9-cpp_offset] = secant_guess;

		}	// End T9 iteration

		// Check that T9_loop converged
		if(T9_iter >= max_iter)
		{
			error_code = 31;
			return;
		}

		// State 3 can now be fully defined
		m_enth_last[3-cpp_offset] = m_enth_last[2-cpp_offset] + Q_dot_LT/m_dot_mc;		// Energy balalnce on cold stream of low-temp recuperator
		prop_error_code = CO2_PH(m_pres_last[3-cpp_offset], m_enth_last[3-cpp_offset], &co2_props);
		if(prop_error_code != 0)
		{
			error_code = prop_error_code;
			return;
		}
		m_temp_last[3-cpp_offset] = co2_props.temp;
		m_entr_last[3-cpp_offset] = co2_props.entr;
		m_dens_last[3-cpp_offset] = co2_props.dens;

		// Go through the mixing valve
		if(ms_des_par.m_recomp_frac >= 1.E-12)
		{
			m_enth_last[4-cpp_offset] = (1.0 - ms_des_par.m_recomp_frac)*m_enth_last[3-cpp_offset] + ms_des_par.m_recomp_frac*m_enth_last[10-cpp_offset];	// conservation of energy (both sides divided by m_dot_t)
			prop_error_code = CO2_PH(m_pres_last[4-cpp_offset], m_enth_last[4-cpp_offset], &co2_props);
			if(prop_error_code != 0)
			{
				error_code = prop_error_code;
				return;
			}
			m_temp_last[4 - cpp_offset] = co2_props.temp;
			m_entr_last[4 - cpp_offset] = co2_props.entr;
			m_dens_last[4 - cpp_offset] = co2_props.dens;
		}
		else		// no mixing valve, therefore state 4 is equal to state 3
		{
			m_temp_last[4-cpp_offset] = m_temp_last[3-cpp_offset];
			m_enth_last[4-cpp_offset] = m_enth_last[3-cpp_offset];
			m_entr_last[4-cpp_offset] = m_entr_last[3-cpp_offset];
			m_dens_last[4-cpp_offset] = m_dens_last[3-cpp_offset];
		}

		// Check for a second law violation at the outlet of the high-temp recuperator
		if(m_temp_last[4-cpp_offset] >= m_temp_last[8-cpp_offset])		// temp(8) is not valid and it must be increased
		{
			T8_lower_bound = m_temp_last[8-cpp_offset];
			m_temp_last[8-cpp_offset] = 0.5*(T8_lower_bound + T8_upper_bound);
			continue;
		}

		// Calculate the UA value of the high-temp recuperator
		if(ms_des_par.m_UA_HT < 1.E-12)			// no high-temp recuperator
			Q_dot_HT = 0.0;
		else
			Q_dot_HT = m_dot_t * (m_enth_last[7-cpp_offset] - m_enth_last[8-cpp_offset]);

		int HT_error_code = 0;
		min_DT_HT = std::numeric_limits<double>::quiet_NaN();

		calculate_hxr_UA_1(ms_des_par.m_N_sub_hxrs, Q_dot_HT, m_dot_t, m_dot_t, m_temp_last[4-cpp_offset], m_temp_last[7-cpp_offset], m_pres_last[4-cpp_offset],
			m_pres_last[5-cpp_offset], m_pres_last[7-cpp_offset], m_pres_last[8-cpp_offset], HT_error_code, UA_HT_calc, min_DT_HT);

		if(HT_error_code != 0)
		{
			if(HT_error_code == 11)			// second-law violation in hxr, therefore temp(8) is too low
			{
				T8_lower_bound = m_temp_last[8-cpp_offset];
				m_temp_last[8-cpp_offset] = 0.5*(T8_lower_bound + T8_upper_bound);	// bisect bounds for next guess
				continue;			
			}
			else
			{
				error_code = HT_error_code;
				return;
			}		
		}

		// Check for convergence and adjust T8 appropriately
		double UA_HT_residual = ms_des_par.m_UA_HT - UA_HT_calc;

		if( fabs(UA_HT_residual) < 1.0E-12 )		// catches no HT case
			break;

		double secant_guess = m_temp_last[8-cpp_offset] - UA_HT_residual*(last_T8_guess - m_temp_last[8-cpp_offset])/(last_HT_residual - UA_HT_residual);		// Next guess predicted using secant method

		if(UA_HT_residual < 0.0)	// UA_HT_calc is too big, temp(8) needs to be higher
		{
			if( fabs(UA_HT_residual) / ms_des_par.m_UA_HT < ms_des_par.m_tol )
				break;
			T8_lower_bound = m_temp_last[8-cpp_offset];
		}
		else						// UA_HT_calc is too small, temp(8) needs to be lower
		{
			if( UA_HT_residual / ms_des_par.m_UA_HT < ms_des_par.m_tol )		// UA_HT converged
				break;
			if( min_DT_HT < temperature_tolerance )								// UA_calc is still too low, but there isn't anywhere to go so it's okay
				break;
			T8_upper_bound = m_temp_last[8-cpp_offset];
		}
		last_HT_residual = UA_HT_residual;				// reset last stored residual value
		last_T8_guess = m_temp_last[8-cpp_offset];		// reset last stored guess value

		// Check if the secant method overshoots and fall back to bisection if it does
		if(secant_guess <= T8_lower_bound || secant_guess >= T8_upper_bound)		// secant method overshot, use bisection
			m_temp_last[8-cpp_offset] = 0.5*(T8_lower_bound + T8_upper_bound);
		else
			m_temp_last[8-cpp_offset] = secant_guess;

	}	// End T8 iteration

	// Check that T8_loop converged
	if(T8_iter >= max_iter)
	{
		error_code = 35;
		return;
	}

	// State 5 can now be fully defined
	m_enth_last[5-cpp_offset] = m_enth_last[4-cpp_offset] + Q_dot_HT/m_dot_t;						// Energy balance on cold stream of high-temp recuperator
	prop_error_code = CO2_PH(m_pres_last[5-cpp_offset], m_enth_last[5-cpp_offset], &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	m_temp_last[5-cpp_offset] = co2_props.temp;
	m_entr_last[5-cpp_offset] = co2_props.entr;
	m_dens_last[5-cpp_offset] = co2_props.dens;

	// Calculate performance metrics for low-temperature recuperator
	C_HeatExchanger::S_design_parameters LT_des_par;
	double C_dot_hot = m_dot_t*(m_enth_last[8-cpp_offset]-m_enth_last[9-cpp_offset])/(m_temp_last[8-cpp_offset]-m_temp_last[9-cpp_offset]);		// LT recuperator hot stream capacitance rate
	double C_dot_cold = m_dot_mc*(m_enth_last[3-cpp_offset]-m_enth_last[2-cpp_offset])/(m_temp_last[3-cpp_offset]-m_temp_last[2-cpp_offset]);	// LT recuperator cold stream capacitance rate
	double C_dot_min = min(C_dot_hot, C_dot_cold);
	double Q_dot_max = C_dot_min*(m_temp_last[8-cpp_offset]-m_temp_last[2-cpp_offset]);
	double hx_eff = Q_dot_LT / Q_dot_max;				// Definition of effectiveness
	LT_des_par.m_DP_design[0] = m_pres_last[2-cpp_offset] - m_pres_last[3-cpp_offset];
	LT_des_par.m_DP_design[1] = m_pres_last[8-cpp_offset] - m_pres_last[9-cpp_offset];
	LT_des_par.m_eff_design = hx_eff;
	LT_des_par.m_min_DT_design = min_DT_LT;
	LT_des_par.m_m_dot_design[0] = m_dot_mc;
	LT_des_par.m_m_dot_design[1] = m_dot_t;
	LT_des_par.m_N_sub = ms_des_par.m_N_sub_hxrs;
	LT_des_par.m_Q_dot_design = Q_dot_LT;
	LT_des_par.m_UA_design = UA_LT_calc;
	m_LT.initialize(LT_des_par);

	// Calculate performance metrics for high-temperature recuperator
	C_HeatExchanger::S_design_parameters HT_des_par;
	C_dot_hot = m_dot_t*(m_enth_last[7-cpp_offset]-m_enth_last[8-cpp_offset])/(m_temp_last[7-cpp_offset]-m_temp_last[8-cpp_offset]);			// HT recuperator hot stream capacitance rate
	C_dot_cold = m_dot_t*(m_enth_last[5-cpp_offset]-m_enth_last[4-cpp_offset])/(m_temp_last[5-cpp_offset]-m_temp_last[4-cpp_offset]);			// HT recuperator cold stream capacitance rate
	C_dot_min = min(C_dot_hot, C_dot_cold);
	Q_dot_max = C_dot_min*(m_temp_last[7-cpp_offset]-m_temp_last[4-cpp_offset]);
	hx_eff = Q_dot_HT / Q_dot_max;						// Definition of effectiveness
	HT_des_par.m_DP_design[0] = m_pres_last[4-cpp_offset] - m_pres_last[5-cpp_offset];
	HT_des_par.m_DP_design[1] = m_pres_last[7-cpp_offset] - m_pres_last[8-cpp_offset];
	HT_des_par.m_eff_design = hx_eff;
	HT_des_par.m_min_DT_design = min_DT_HT;
	HT_des_par.m_m_dot_design[0] = m_dot_t;
	HT_des_par.m_m_dot_design[1] = m_dot_t;
	HT_des_par.m_N_sub = ms_des_par.m_N_sub_hxrs;
	HT_des_par.m_Q_dot_design = Q_dot_HT;
	HT_des_par.m_UA_design = UA_HT_calc;
	m_HT.initialize(HT_des_par);

	// Set relevant values for other heat exchangers
	C_HeatExchanger::S_design_parameters PHX_des_par;
	PHX_des_par.m_DP_design[0] = m_pres_last[5-cpp_offset] - m_pres_last[6-cpp_offset];
	PHX_des_par.m_DP_design[1] = 0.0;
	PHX_des_par.m_m_dot_design[0] = m_dot_t;
	PHX_des_par.m_m_dot_design[1] = 0.0;
	PHX_des_par.m_Q_dot_design = m_dot_t*(m_enth_last[6-cpp_offset] - m_enth_last[5-cpp_offset]);
	m_PHX.initialize(PHX_des_par);

	C_HeatExchanger::S_design_parameters PC_des_par;
	PC_des_par.m_DP_design[0] = 0.0;
	PC_des_par.m_DP_design[1] = m_pres_last[9-cpp_offset] - m_pres_last[1-cpp_offset];
	PC_des_par.m_m_dot_design[0] = 0.0;
	PC_des_par.m_m_dot_design[1] = m_dot_mc;
	PC_des_par.m_Q_dot_design = m_dot_mc*(m_enth_last[9-cpp_offset] - m_enth_last[1-cpp_offset]);
	m_PC.initialize(PC_des_par);

	// Calculate/set cycle performance metrics
	m_W_dot_net_last = w_mc*m_dot_mc + w_rc*m_dot_rc + w_t*m_dot_t;
	m_eta_thermal_last = m_W_dot_net_last / PHX_des_par.m_Q_dot_design;

	m_m_dot_mc = m_dot_mc;
	m_m_dot_rc = m_dot_rc;
	m_m_dot_t = m_dot_t;
}

void C_RecompCycle::design(S_design_parameters & des_par_in, int & error_code)
{
	ms_des_par = des_par_in;

	int design_error_code = 0;
	
	design_core(design_error_code);

	if(design_error_code != 0)
	{
		error_code = design_error_code;
		return;
	}

	finalize_design(design_error_code);

	error_code = design_error_code;
}

void C_RecompCycle::opt_design(S_opt_design_parameters & opt_des_par_in, int & error_code)
{
	ms_opt_des_par = opt_des_par_in;

	int opt_design_error_code = 0;

	opt_design_core(error_code);

	if(opt_design_error_code != 0)
	{
		error_code = opt_design_error_code;
		return;
	}

	finalize_design(opt_design_error_code);

	error_code = opt_design_error_code;
}

void C_RecompCycle::opt_design_core(int & error_code)
{
	// Map ms_opt_des_par to ms_des_par
	ms_des_par.m_W_dot_net = ms_opt_des_par.m_W_dot_net;
	ms_des_par.m_T_mc_in = ms_opt_des_par.m_T_mc_in;
	ms_des_par.m_T_t_in = ms_opt_des_par.m_T_t_in;
	ms_des_par.m_DP_LT = ms_opt_des_par.m_DP_LT;
	ms_des_par.m_DP_HT = ms_opt_des_par.m_DP_HT;
	ms_des_par.m_DP_PC = ms_opt_des_par.m_DP_PC;
	ms_des_par.m_DP_PHX = ms_opt_des_par.m_DP_PHX;
	ms_des_par.m_eta_mc = ms_opt_des_par.m_eta_mc;
	ms_des_par.m_eta_rc = ms_opt_des_par.m_eta_rc;
	ms_des_par.m_eta_t = ms_opt_des_par.m_eta_t;
	ms_des_par.m_N_sub_hxrs = ms_opt_des_par.m_N_sub_hxrs;
	ms_des_par.m_P_high_limit = ms_opt_des_par.m_P_high_limit;
	ms_des_par.m_tol = ms_opt_des_par.m_tol;
	ms_des_par.m_N_turbine = ms_opt_des_par.m_N_turbine;

	// ms_des_par members to be defined by optimizer and set in 'design_point_eta':
		// m_P_mc_in
		// m_P_mc_out
		// m_recomp_frac
		// m_UA_LT
		// m_UA_HT

	int index = 0;

	std::vector<double> x(0);
	std::vector<double> lb(0);
	std::vector<double> ub(0);
	std::vector<double> scale(0);

	if( !ms_opt_des_par.m_fixed_P_mc_out )
	{
		x.push_back(ms_opt_des_par.m_P_mc_out_guess);
		lb.push_back(100.0);
		ub.push_back(ms_opt_des_par.m_P_high_limit);
		scale.push_back(500.0);

		index++;
	}

	if( !ms_opt_des_par.m_fixed_PR_mc )
	{
		x.push_back(ms_opt_des_par.m_PR_mc_guess);
		lb.push_back(0.0001);
		double PR_max = ms_opt_des_par.m_P_high_limit / 100.0;
		ub.push_back(PR_max);
		scale.push_back(0.2);

		index++;
	}

	if( !ms_opt_des_par.m_fixed_recomp_frac )
	{
		x.push_back(ms_opt_des_par.m_recomp_frac_guess);
		lb.push_back(0.0);
		ub.push_back(1.0);
		scale.push_back(0.05);

		index++;
	}

	if( !ms_opt_des_par.m_fixed_LT_frac )
	{
		x.push_back(ms_opt_des_par.m_LT_frac_guess);
		lb.push_back(0.0);
		ub.push_back(1.0);
		scale.push_back(0.05);

		index++;
	}

	int no_opt_error_code = 0;
	if( index > 0 )
	{
		// Ensure thermal efficiency is initialized to 0
		m_eta_thermal_opt = 0.0;

		// Set up instance of nlopt class and set optimization parameters
		nlopt::opt		opt_des_cycle(nlopt::LN_SBPLX, index);
		opt_des_cycle.set_lower_bounds(lb);
		opt_des_cycle.set_upper_bounds(ub);
		opt_des_cycle.set_initial_step(scale);
		opt_des_cycle.set_xtol_rel(ms_opt_des_par.m_opt_tol);

		// Set max objective function
		opt_des_cycle.set_max_objective(nlopt_callback_opt_des_1, this);		// Calls wrapper/callback that calls 'design_point_eta', which optimizes design point eta through repeated calls to 'design'
		double max_f = std::numeric_limits<double>::quiet_NaN();
		nlopt::result   result_des_cycle = opt_des_cycle.optimize(x, max_f);
		
		ms_des_par = ms_des_par_optimal;

		design_core(no_opt_error_code);

		/*
		m_W_dot_net_last = m_W_dot_net_opt;
		m_eta_thermal_last = m_eta_thermal_opt;
		m_temp_last = m_temp_opt;
		m_pres_last = m_pres_opt;
		m_enth_last = m_enth_opt;
		m_entr_last = m_entr_opt;
		m_dens_last = m_dens_opt;
		*/
	}
	else
	{
		// Finish defining ms_des_par based on current 'x' values
		ms_des_par.m_P_mc_out = ms_opt_des_par.m_P_mc_out_guess;
		ms_des_par.m_P_mc_in = ms_des_par.m_P_mc_out / ms_opt_des_par.m_PR_mc_guess;
		ms_des_par.m_recomp_frac = ms_opt_des_par.m_recomp_frac_guess;
		ms_des_par.m_UA_LT = ms_opt_des_par.m_UA_rec_total*ms_opt_des_par.m_LT_frac_guess;
		ms_des_par.m_UA_HT = ms_opt_des_par.m_UA_rec_total*(1.0 - ms_opt_des_par.m_LT_frac_guess);
		
		design_core(no_opt_error_code);

		ms_des_par_optimal = ms_des_par;
	}

}

double C_RecompCycle::design_point_eta(const std::vector<double> &x)
{
	// 'x' is array of inputs either being adjusted by optimizer or set constant
	// Finish defining ms_des_par based on current 'x' values

	int index = 0;

	// Main compressor outlet pressure
	if( !ms_opt_des_par.m_fixed_P_mc_out )
	{
		ms_des_par.m_P_mc_out = x[index];
		if( ms_des_par.m_P_mc_out > ms_opt_des_par.m_P_high_limit )
			return 0.0;
		index++;
	}
	else
		ms_des_par.m_P_mc_out = ms_opt_des_par.m_P_mc_out_guess;

	// Main compressor pressure ratio
	double PR_mc_local = -999.9;
	if( !ms_opt_des_par.m_fixed_PR_mc )
	{
		PR_mc_local = x[index];
		if( PR_mc_local > 50.0 )
			return 0.0;
		index++;
	}
	else
		PR_mc_local = ms_opt_des_par.m_PR_mc_guess;
	
	double P_mc_in = ms_des_par.m_P_mc_out / PR_mc_local;
	if( P_mc_in >= ms_des_par.m_P_mc_out )
		return 0.0;
	if( P_mc_in <= 100.0 )
		return 0.0;
	ms_des_par.m_P_mc_in = P_mc_in;

	// Recompression fraction
	if( !ms_opt_des_par.m_fixed_recomp_frac )
	{
		ms_des_par.m_recomp_frac = x[index];
		if( ms_des_par.m_recomp_frac < 0.0 )
			return 0.0;
		index++;
	}
	else
		ms_des_par.m_recomp_frac = ms_opt_des_par.m_recomp_frac_guess;

	// Recuperator split fraction
	double LT_frac_local = -999.9;
	if( !ms_opt_des_par.m_fixed_LT_frac )
	{
		LT_frac_local = x[index];
		if( LT_frac_local > 1.0 || LT_frac_local < 0.0 )
			return 0.0;
		index++;
	}
	else
		LT_frac_local = ms_opt_des_par.m_LT_frac_guess;
	
	ms_des_par.m_UA_LT = ms_opt_des_par.m_UA_rec_total*LT_frac_local;
	ms_des_par.m_UA_HT = ms_opt_des_par.m_UA_rec_total*(1.0 - LT_frac_local);

	int error_code = 0;

	design_core(error_code);

	double eta_thermal = 0.0;
	if( error_code == 0 )
	{
		eta_thermal = m_eta_thermal_last;

		if( m_eta_thermal_last > m_eta_thermal_opt )
		{
			ms_des_par_optimal = ms_des_par;
			m_eta_thermal_opt = m_eta_thermal_last;
		}
	}

	return eta_thermal;
}

void C_RecompCycle::auto_opt_design(S_auto_opt_design_parameters & auto_opt_des_par_in, int & error_code)
{
	ms_auto_opt_des_par = auto_opt_des_par_in;

	// map 'auto_opt_des_par_in' to 'ms_auto_opt_des_par'
	ms_opt_des_par.m_W_dot_net = ms_auto_opt_des_par.m_W_dot_net;
	ms_opt_des_par.m_T_mc_in = ms_auto_opt_des_par.m_T_mc_in;
	ms_opt_des_par.m_T_t_in = ms_auto_opt_des_par.m_T_t_in;
	ms_opt_des_par.m_DP_LT = ms_auto_opt_des_par.m_DP_LT;
	ms_opt_des_par.m_DP_HT = ms_auto_opt_des_par.m_DP_HT;
	ms_opt_des_par.m_DP_PC = ms_auto_opt_des_par.m_DP_PC;
	ms_opt_des_par.m_DP_PHX = ms_auto_opt_des_par.m_DP_PHX;
	ms_opt_des_par.m_UA_rec_total = ms_auto_opt_des_par.m_UA_rec_total;
	ms_opt_des_par.m_eta_mc = ms_auto_opt_des_par.m_eta_mc;
	ms_opt_des_par.m_eta_rc = ms_auto_opt_des_par.m_eta_rc;
	ms_opt_des_par.m_eta_t = ms_auto_opt_des_par.m_eta_t;
	ms_opt_des_par.m_N_sub_hxrs = ms_auto_opt_des_par.m_N_sub_hxrs;
	ms_opt_des_par.m_P_high_limit = ms_auto_opt_des_par.m_P_high_limit;
	ms_opt_des_par.m_tol = ms_auto_opt_des_par.m_tol;
	ms_opt_des_par.m_opt_tol = ms_auto_opt_des_par.m_opt_tol;
	ms_opt_des_par.m_N_turbine = ms_auto_opt_des_par.m_N_turbine;

	// Outer optimization loop
	m_eta_thermal_auto_opt = 0.0;

	double best_P_high = fminbr(
		ms_auto_opt_des_par.m_P_high_limit*0.2, ms_auto_opt_des_par.m_P_high_limit, &fmin_callback_opt_eta_1, this, 1.0);

	// Check model with P_mc_out set at P_high_limit for a recompression and simple cycle and use the better configuration
	double PR_mc_guess = ms_des_par_auto_opt.m_P_mc_out / ms_des_par_auto_opt.m_P_mc_in;

	// Complete 'ms_opt_des_par' for recompression cycle
	ms_opt_des_par.m_P_mc_out_guess = ms_auto_opt_des_par.m_P_high_limit;
	ms_opt_des_par.m_fixed_P_mc_out = true;
	ms_opt_des_par.m_PR_mc_guess = PR_mc_guess;
	ms_opt_des_par.m_fixed_PR_mc = false;
	ms_opt_des_par.m_recomp_frac_guess = 0.3;
	ms_opt_des_par.m_fixed_recomp_frac = false;
	ms_opt_des_par.m_LT_frac_guess = 0.5;
	ms_opt_des_par.m_fixed_LT_frac = false;

	int rc_error_code = 0;

	opt_design_core(rc_error_code);

	if( rc_error_code == 0 && m_eta_thermal_opt > m_eta_thermal_auto_opt )
	{
		ms_des_par_auto_opt = ms_des_par_optimal;
		m_eta_thermal_auto_opt = m_eta_thermal_opt;
	}

	// Complete 'ms_opt_des_par' for simple cycle
	ms_opt_des_par.m_P_mc_out_guess = ms_auto_opt_des_par.m_P_high_limit;
	ms_opt_des_par.m_fixed_P_mc_out = true;
	ms_opt_des_par.m_PR_mc_guess = PR_mc_guess;
	ms_opt_des_par.m_fixed_PR_mc = false;
	ms_opt_des_par.m_recomp_frac_guess = 0.0;
	ms_opt_des_par.m_fixed_recomp_frac = true;
	ms_opt_des_par.m_LT_frac_guess = 0.5;
	ms_opt_des_par.m_fixed_LT_frac = true;

	int s_error_code = 0;

	opt_design_core(s_error_code);

	if( s_error_code == 0 && m_eta_thermal_opt > m_eta_thermal_auto_opt )
	{
		ms_des_par_auto_opt = ms_des_par_optimal;
		m_eta_thermal_auto_opt = m_eta_thermal_opt;
	}

	ms_des_par = ms_des_par_auto_opt;

	int optimal_design_error_code = 0;
	design_core(optimal_design_error_code);

	if(optimal_design_error_code != 0)
	{
		error_code = optimal_design_error_code;
		return;
	}

	finalize_design(optimal_design_error_code);

	error_code = optimal_design_error_code;
}

double C_RecompCycle::opt_eta(double P_high_opt)
{
	double PR_mc_guess = 1.1;
	if(P_high_opt > P_pseudocritical_1(ms_opt_des_par.m_T_mc_in))
		PR_mc_guess = P_high_opt / P_pseudocritical_1(ms_opt_des_par.m_T_mc_in);

	// Complete 'ms_opt_des_par' for recompression cycle
	ms_opt_des_par.m_P_mc_out_guess = P_high_opt;
	ms_opt_des_par.m_fixed_P_mc_out = true;
	ms_opt_des_par.m_PR_mc_guess = PR_mc_guess;
	ms_opt_des_par.m_fixed_PR_mc = false;
	ms_opt_des_par.m_recomp_frac_guess = 0.3;
	ms_opt_des_par.m_fixed_recomp_frac = false;
	ms_opt_des_par.m_LT_frac_guess = 0.5;
	ms_opt_des_par.m_fixed_LT_frac = false;

	int rc_error_code = 0;
	opt_design_core(rc_error_code);

	int local_eta_rc = 0.0;
	if( rc_error_code == 0 )
		local_eta_rc = m_eta_thermal_opt;
	
	if(rc_error_code == 0 && m_eta_thermal_opt > m_eta_thermal_auto_opt)
	{
		ms_des_par_auto_opt = ms_des_par_optimal;
		m_eta_thermal_auto_opt = m_eta_thermal_opt;
	}

	// Complete 'ms_opt_des_par' for simple cycle
	ms_opt_des_par.m_P_mc_out_guess = P_high_opt;
	ms_opt_des_par.m_fixed_P_mc_out = true;
	ms_opt_des_par.m_PR_mc_guess = PR_mc_guess;
	ms_opt_des_par.m_fixed_PR_mc = false;
	ms_opt_des_par.m_recomp_frac_guess = 0.0;
	ms_opt_des_par.m_fixed_recomp_frac = true;
	ms_opt_des_par.m_LT_frac_guess = 0.5;
	ms_opt_des_par.m_fixed_LT_frac = true;

	int s_error_code = 0;
	opt_design_core(s_error_code);

	int local_eta_s = 0.0;
	if( s_error_code == 0 )
		local_eta_s = m_eta_thermal_opt;

	if(s_error_code == 0 && m_eta_thermal_opt > m_eta_thermal_auto_opt)
	{
		ms_des_par_auto_opt = ms_des_par_optimal;
		m_eta_thermal_auto_opt = m_eta_thermal_opt;
	}

	return -max(local_eta_rc, local_eta_s);

}

void C_RecompCycle::finalize_design(int & error_code)
{
	int cpp_offset = 1;

	// Size main compressor
	C_compressor::S_design_parameters  mc_des_par;
		// Compressor inlet conditions
	mc_des_par.m_D_in = m_dens_last[1-cpp_offset];
	mc_des_par.m_h_in = m_enth_last[1-cpp_offset];
	mc_des_par.m_s_in = m_entr_last[1-cpp_offset];
		// Compressor outlet conditions
	mc_des_par.m_T_out = m_temp_last[2-cpp_offset];
	mc_des_par.m_P_out = m_pres_last[2-cpp_offset];
	mc_des_par.m_h_out = m_enth_last[2-cpp_offset];
	mc_des_par.m_D_out = m_dens_last[2-cpp_offset];
		// Mass flow
	mc_des_par.m_m_dot = m_m_dot_mc;

	int comp_size_error_code = 0;
	m_mc.compressor_sizing(mc_des_par, comp_size_error_code);
	if(comp_size_error_code != 0)
	{
		error_code = comp_size_error_code;
		return;
	}

	if( ms_des_par.m_recomp_frac > 0.01 )
	{
		// Size recompressor
		C_recompressor::S_design_parameters  rc_des_par;
		// Compressor inlet conditions
		rc_des_par.m_P_in = m_pres_last[9 - cpp_offset];
		rc_des_par.m_D_in = m_dens_last[9 - cpp_offset];
		rc_des_par.m_h_in = m_enth_last[9 - cpp_offset];
		rc_des_par.m_s_in = m_entr_last[9 - cpp_offset];
		// Compressor outlet conditions
		rc_des_par.m_T_out = m_temp_last[10 - cpp_offset];
		rc_des_par.m_P_out = m_pres_last[10 - cpp_offset];
		rc_des_par.m_h_out = m_enth_last[10 - cpp_offset];
		rc_des_par.m_D_out = m_dens_last[10 - cpp_offset];
		// Mass flow
		rc_des_par.m_m_dot = m_m_dot_rc;

		int recomp_size_error_code = 0;
		m_rc.recompressor_sizing(rc_des_par, recomp_size_error_code);
		if( recomp_size_error_code != 0 )
		{
			error_code = recomp_size_error_code;
			return;
		}

		ms_des_solved.m_is_rc = true;
	}
	else
		ms_des_solved.m_is_rc = false;
	
	// Size turbine
	C_turbine::S_design_parameters  t_des_par;
		// Set turbine shaft speed
	t_des_par.m_N_design = ms_des_par.m_N_turbine;
	t_des_par.m_N_comp_design_if_linked = m_mc.get_design_solved()->m_N_design;
		// Turbine inlet state
	t_des_par.m_P_in = m_pres_last[6-cpp_offset];
	t_des_par.m_T_in = m_temp_last[6-cpp_offset];
	t_des_par.m_D_in = m_dens_last[6-cpp_offset];
	t_des_par.m_h_in = m_enth_last[6-cpp_offset];
	t_des_par.m_s_in = m_entr_last[6-cpp_offset];
		// Turbine outlet state
	t_des_par.m_P_out = m_pres_last[7-cpp_offset];
	t_des_par.m_h_out = m_enth_last[7-cpp_offset];
		// Mass flow
	t_des_par.m_m_dot = m_m_dot_t;

	int turb_size_error_code = 0;
	m_t.turbine_sizing(t_des_par, turb_size_error_code);
	if(turb_size_error_code != 0)
	{
		error_code = turb_size_error_code;
		return;
	}

	// Set solved design point metrics
	ms_des_solved.m_temp = m_temp_last;
	ms_des_solved.m_pres = m_pres_last;
	ms_des_solved.m_enth = m_enth_last;
	ms_des_solved.m_entr = m_entr_last;
	ms_des_solved.m_dens = m_dens_last;

	ms_des_solved.m_eta_thermal = m_eta_thermal_last;
	ms_des_solved.m_W_dot_net = m_W_dot_net_last;
	ms_des_solved.m_m_dot_mc = m_m_dot_mc;
	ms_des_solved.m_m_dot_rc = m_m_dot_rc;
	ms_des_solved.m_m_dot_t = m_m_dot_t;
	ms_des_solved.m_recomp_frac = m_m_dot_rc / m_m_dot_t;

	ms_des_solved.m_N_mc = m_mc.get_design_solved()->m_N_design;
	ms_des_solved.m_N_t = m_t.get_design_solved()->m_N_design;

	ms_des_solved.m_UA_LT = ms_des_par.m_UA_LT;
	ms_des_solved.m_UA_HT = ms_des_par.m_UA_HT;

}

void C_RecompCycle::off_design(S_od_parameters & od_par_in, int & error_code)
{
	ms_od_par = od_par_in;

	int od_error_code = 0;

	off_design_core(od_error_code);

	error_code = od_error_code;
}

void C_RecompCycle::off_design_core(int & error_code)
{	
	CO2_state co2_props;

	int cpp_offset = 1;

	// Initialize a few variables
	m_temp_od[1-cpp_offset] = ms_od_par.m_T_mc_in;
	m_pres_od[1-cpp_offset] = ms_od_par.m_P_mc_in;
	m_temp_od[6-cpp_offset] = ms_od_par.m_T_t_in;

	// mc.N          t.N           tol

	// Prepare the mass flow rate iteration loop
	int prop_error_code = CO2_TP(m_temp_od[1-cpp_offset], m_pres_od[1-cpp_offset], &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	double rho_in = co2_props.dens;

	double tip_speed = m_mc.get_design_solved()->m_D_rotor * 0.5 * ms_od_par.m_N_mc * 0.10471975512;	//[m/s] Main compressor tip speed
	double partial_phi = rho_in*pow(m_mc.get_design_solved()->m_D_rotor,2)*tip_speed;					// ... reduces computation on next two lines
	double m_dot_mc_guess = C_compressor::m_snl_phi_design * partial_phi;					//[kg/s] mass flow rate corresponding to design-point phi in main compressor
	double m_dot_mc_max = C_compressor::m_snl_phi_max * partial_phi * 1.2;					//[kg/s] largest possible mass flow rate in main compressor (with safety factor)
	double m_dot_t = m_dot_mc_guess/(1.0 - ms_od_par.m_recomp_frac);						//[kg/s] first guess for mass flow rate through turbine
	double m_dot_upper_bound = m_dot_mc_max/(1.0 - ms_od_par.m_recomp_frac);				//[kg/s] largest possible mass flow rate through turbine
	double m_dot_lower_bound = 0.0;															//[-] this lower bound allows for surge (checked after iteration)
	bool first_pass = true;

	// Enter the mass flow rate iteration loop
	int m_dot_iter = -1;
	int max_iter = 100;
	double temperature_tolerance = 1.E-6;

	double m_dot_rc = std::numeric_limits<double>::quiet_NaN();
	double m_dot_mc = std::numeric_limits<double>::quiet_NaN();

	double last_m_dot_guess = -999.9;
	double last_m_dot_residual = std::numeric_limits<double>::quiet_NaN();

	for( m_dot_iter = 0; m_dot_iter < max_iter; m_dot_iter++ )
	{
		m_dot_rc = m_dot_t * ms_od_par.m_recomp_frac;
		m_dot_mc = m_dot_t - m_dot_rc;

		// Calculate the pressure rise through the main compressor
		int comp_error_code = 0;
		m_mc.off_design_compressor(m_temp_od[1-cpp_offset], m_pres_od[1-cpp_offset], m_dot_mc, ms_od_par.m_N_mc, 
			comp_error_code, m_temp_od[2-cpp_offset], m_pres_od[2-cpp_offset]);

		if(comp_error_code == 1)			// m_dot is too high because the given shaft speed is not possible
		{
			m_dot_upper_bound = m_dot_t;
			m_dot_t = 0.5*(m_dot_lower_bound + m_dot_upper_bound);		// use bisection for new mass flow rate guess
			continue;
		}
		else if(comp_error_code == 2)		// m_dot is too low because P_out is (likely) above properties limits
		{
			m_dot_lower_bound = m_dot_t;
			m_dot_t = 0.5*(m_dot_lower_bound + m_dot_upper_bound);		// use bisection for new mass flow rate guess
			continue;
		}
		else if(comp_error_code != 0)		// unexpected error
		{
			error_code = comp_error_code;
			return;
		}

		// Calculate scaled pressure drops through heat exchangers.
		std::vector<double> DP_LT, DP_HT, DP_PHX, DP_PC;
		std::vector<double> m_dot_LT;
		m_dot_LT.push_back( m_dot_mc );
		m_dot_LT.push_back( m_dot_t );
		m_LT.hxr_pressure_drops(m_dot_LT, DP_LT);

		std::vector<double> m_dot_HT;
		m_dot_HT.push_back( m_dot_t );
		m_dot_HT.push_back( m_dot_t );
		m_HT.hxr_pressure_drops(m_dot_HT, DP_HT);
		
		std::vector<double> m_dot_PHX;
		m_dot_PHX.push_back( m_dot_t );
		m_dot_PHX.push_back( 0.0 );
		m_PHX.hxr_pressure_drops(m_dot_PHX, DP_PHX);
		
		std::vector<double> m_dot_PC;
		m_dot_PC.push_back( 0.0 );
		m_dot_PC.push_back( m_dot_mc);
		m_PC.hxr_pressure_drops(m_dot_PC, DP_PC);

		// Apply pressure drops to heat exchangers, fully defining the pressure at all states
		m_pres_od[3-cpp_offset] = m_pres_od[2-cpp_offset] - DP_LT[1-cpp_offset];		// LT recuperator (cold stream)
		m_pres_od[4-cpp_offset] = m_pres_od[3-cpp_offset];								// Assume no pressure drop in mixing valve
		m_pres_od[10-cpp_offset] = m_pres_od[3-cpp_offset];								// Assume no pressure drop in mixing valve
		m_pres_od[5-cpp_offset] = m_pres_od[4-cpp_offset] - DP_HT[1-cpp_offset];		// HT recuperator (cold stream)
		m_pres_od[6-cpp_offset] = m_pres_od[5-cpp_offset] - DP_PHX[1-cpp_offset];		// PHX
		m_pres_od[9-cpp_offset] = m_pres_od[1-cpp_offset] + DP_PC[2-cpp_offset];		// precooler
		m_pres_od[8-cpp_offset] = m_pres_od[9-cpp_offset] + DP_LT[2-cpp_offset];		// LT recuperator (hot stream)
		m_pres_od[7-cpp_offset] = m_pres_od[8-cpp_offset] + DP_HT[2-cpp_offset];		// HT recuperator (hot stream)

		// Calculate the mass flow rate through the turbine
		int turbine_error_code = 0;
		double m_dot_t_allowed = std::numeric_limits<double>::quiet_NaN();
		m_t.off_design_turbine(m_temp_od[6-cpp_offset], m_pres_od[6-cpp_offset], m_pres_od[7-cpp_offset], ms_od_par.m_N_t,
			turbine_error_code, m_dot_t_allowed, m_temp_od[7-cpp_offset]);
		
		if(turbine_error_code != 0)		// unexpected error
		{
			error_code = turbine_error_code;
			return;
		}

		// Determine the mass flow rate residual and prepare the next iteration
		double m_dot_residual = m_dot_t - m_dot_t_allowed;
		
		// twn: during the first iteration 'last_m_dot_guess' won't be initialized
		double secant_guess = m_dot_t - m_dot_residual*(last_m_dot_guess-m_dot_t)/(last_m_dot_residual-m_dot_residual);		// next guess predicted using secant method
		
		if(m_dot_residual > 0.0)	// pressure rise is too small, so m_dot_t is too big
		{
			if( m_dot_residual / m_dot_t < ms_od_par.m_tol )		// residual is positive; check for convergence
				break;

			m_dot_upper_bound = m_dot_t;		// reset upper bound
		}
		else	// pressure rise is too high, so m_dot_t is too small
		{
			if( -m_dot_residual / m_dot_t < ms_od_par.m_tol )	// residual is negative; check for converge
				break;

			m_dot_lower_bound = m_dot_t;		// reset lower bound
		}

		last_m_dot_residual = m_dot_residual;		// reset last stored residual value
		last_m_dot_guess = m_dot_t;					// reset last stored guess value

		// Check if the secant method overshoots and fall back to bisection if it does
		if(first_pass)
		{
			m_dot_t = 0.5*(m_dot_upper_bound + m_dot_lower_bound);
			first_pass = false;
		}
		else if(secant_guess < m_dot_lower_bound || secant_guess > m_dot_upper_bound)		// secant method overshot, use bisection
			m_dot_t = 0.5*(m_dot_upper_bound + m_dot_lower_bound);
		else
			m_dot_t = secant_guess;
	
	}	// End m_dot loop

	// Check for convergence
	if(m_dot_iter >= max_iter)
	{
		error_code = 42;
		return;
	}

	// Fully define known states
	prop_error_code = CO2_TP(m_temp_od[1-cpp_offset], m_pres_od[1-cpp_offset], &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	m_enth_od[1-cpp_offset] = co2_props.enth;
	m_entr_od[1-cpp_offset] = co2_props.entr;
	m_dens_od[1-cpp_offset] = co2_props.dens;

	prop_error_code = CO2_TP(m_temp_od[2-cpp_offset], m_pres_od[2-cpp_offset], &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	m_enth_od[2-cpp_offset] = co2_props.enth;
	m_entr_od[2-cpp_offset] = co2_props.entr;
	m_dens_od[2-cpp_offset] = co2_props.dens;

	prop_error_code = CO2_TP(m_temp_od[6-cpp_offset], m_pres_od[6-cpp_offset], &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	m_enth_od[6-cpp_offset] = co2_props.enth;
	m_entr_od[6-cpp_offset] = co2_props.entr;
	m_dens_od[6-cpp_offset] = co2_props.dens;

	prop_error_code = CO2_TP(m_temp_od[7-cpp_offset], m_pres_od[7-cpp_offset], &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	m_enth_od[7-cpp_offset] = co2_props.enth;
	m_entr_od[7-cpp_offset] = co2_props.entr;
	m_dens_od[7-cpp_offset] = co2_props.dens;

	// Get the recuperator conductances corresponding to the converged mass flow rates
	double UA_LT, UA_HT;
	UA_LT = UA_HT = std::numeric_limits<double>::quiet_NaN();
	std::vector<double> m_dot_LT;
	m_dot_LT.push_back( m_dot_mc );
	m_dot_LT.push_back( m_dot_t );
	std::vector<double> m_dot_HT;
	m_dot_HT.push_back( m_dot_t );
	m_dot_HT.push_back( m_dot_t );
	m_LT.hxr_conductance(m_dot_LT, UA_LT);
	m_HT.hxr_conductance(m_dot_HT, UA_HT);

	// Outer iteration loop: temp(8), checking against UA_HT
	double T8_lower_bound = std::numeric_limits<double>::quiet_NaN();
	double T8_upper_bound = std::numeric_limits<double>::quiet_NaN();
	double UA_HT_calc = std::numeric_limits<double>::quiet_NaN();
	double last_HT_residual = std::numeric_limits<double>::quiet_NaN();
	double last_T8_guess = std::numeric_limits<double>::quiet_NaN();
	if(UA_HT < 1.E-12)		// no high-temperature recuperator
	{
		T8_lower_bound = m_temp_od[7-cpp_offset];	// no iteration necessary
		T8_upper_bound = m_temp_od[7-cpp_offset];	// no iteration necessary
		m_temp_od[8-cpp_offset] = m_temp_od[7-cpp_offset];
		UA_HT_calc = 0.0;
		last_HT_residual = 0.0;
		last_T8_guess = m_temp_od[7-cpp_offset];
	}
	else
	{
		T8_lower_bound = m_temp_od[2-cpp_offset];	// the absolute lowest temp(8) could be
		T8_upper_bound = m_temp_od[7-cpp_offset];	// the absolute highest temp(8) could be
		m_temp_od[8-cpp_offset] = 0.5*(T8_lower_bound + T8_upper_bound);	// bisect bounds for first guess
		UA_HT_calc = -1.0;
		last_HT_residual = UA_HT;					// know a priori that with T8 = T7, UA_calc = 0 therefore residual is UA_HT - 0.0
		last_T8_guess = m_temp_od[7-cpp_offset];
	}

	int T8_iter = -1;

	double Q_dot_LT = std::numeric_limits<double>::quiet_NaN();
	double Q_dot_HT = std::numeric_limits<double>::quiet_NaN();

	for( T8_iter = 0; T8_iter < max_iter; T8_iter++ )
	{
		// Fully define state 8
		prop_error_code = CO2_TP(m_temp_od[8-cpp_offset], m_pres_od[8-cpp_offset], &co2_props);
		if(prop_error_code != 0)
		{
			error_code = prop_error_code;
			return;
		}
		m_enth_od[8-cpp_offset] = co2_props.enth;
		m_entr_od[8-cpp_offset] = co2_props.entr;
		m_dens_od[8-cpp_offset] = co2_props.dens;
	
		// Inner iteration loop: temp(9), checking against UA_LT
		double T9_lower_bound = std::numeric_limits<double>::quiet_NaN();
		double T9_upper_bound = std::numeric_limits<double>::quiet_NaN();
		double UA_LT_calc = std::numeric_limits<double>::quiet_NaN();
		double last_LT_residual = std::numeric_limits<double>::quiet_NaN();
		double last_T9_guess = std::numeric_limits<double>::quiet_NaN();
		if(UA_LT < 1.E-12)		// no low-temperature recuperator
		{
			T9_lower_bound = m_temp_od[8-cpp_offset];	// no iteration necessary
			T9_upper_bound = m_temp_od[8-cpp_offset];	// no iteration necessary
			m_temp_od[9-cpp_offset] = m_temp_od[8-cpp_offset];	
			UA_LT_calc = 0.0;
			last_LT_residual = 0.0;
			last_T9_guess = m_temp_od[8-cpp_offset];
		}
		else
		{
			T9_lower_bound = m_temp_od[2-cpp_offset];		// the absolute lowest temp(9) could be
			T9_upper_bound = m_temp_od[8-cpp_offset];		// the absolute highest temp(9) could be
			m_temp_od[9-cpp_offset] = 0.5*(T9_lower_bound + T9_upper_bound);	// bisect bounds for first guess
			UA_LT_calc = -1.0;
			last_LT_residual = UA_LT;			// know a priori that with T9=T8, UA_calc = 0 therefore residual is UA_LT - 0
			last_T9_guess = m_temp_od[8-cpp_offset];
		}

		int T9_iter = -1;

		for( T9_iter = 0; T9_iter < max_iter; T9_iter++ )
		{
			prop_error_code = CO2_TP(m_temp_od[9-cpp_offset], m_pres_od[9-cpp_offset], &co2_props);		// fully define state 9
			if(prop_error_code != 0)
			{
				error_code = prop_error_code;
				return;
			}
			m_enth_od[9-cpp_offset] = co2_props.enth;
			m_entr_od[9-cpp_offset] = co2_props.entr;
			m_dens_od[9-cpp_offset] = co2_props.dens;
		
			if(ms_od_par.m_recomp_frac >= 1.E-12)		// determine the required shaft speed for the recompressing compressor
			{
				int rc_error_code = 0;
				m_rc.off_design_recompressor(m_temp_od[9-cpp_offset], m_pres_od[9-cpp_offset], m_dot_rc, m_pres_od[10-cpp_offset], rc_error_code, m_temp_od[10-cpp_offset]);
			
				if(rc_error_code != 0)
				{
					error_code = rc_error_code;
					return;
				}

				// Fully define state 10
				prop_error_code = CO2_TP(m_temp_od[10-cpp_offset], m_pres_od[10-cpp_offset], &co2_props);
				if(prop_error_code != 0)
				{
					error_code = prop_error_code;
					return;
				}
				m_enth_od[10-cpp_offset] = co2_props.enth;
				m_entr_od[10-cpp_offset] = co2_props.entr;
				m_dens_od[10-cpp_offset] = co2_props.dens;			
			}
			else
			{
				m_temp_od[10-cpp_offset] = m_temp_od[9-cpp_offset];
				m_enth_od[10-cpp_offset] = m_enth_od[9-cpp_offset];
				m_entr_od[10-cpp_offset] = m_entr_od[9-cpp_offset];
				m_dens_od[10-cpp_offset] = m_dens_od[9-cpp_offset];
			}

			// Calculate the UA value of the low-temperature recuperator
			Q_dot_LT = std::numeric_limits<double>::quiet_NaN();
			if( UA_LT < 1.E-12 )			// no low-temp recuperator (this check is necessary to prevent pressure drops with ua=0 from causing problems)
				Q_dot_LT = 0.0;
			else
				Q_dot_LT = m_dot_t * (m_enth_od[8-cpp_offset] - m_enth_od[9-cpp_offset]);

			int hx_error_code = 0;
			double min_DT_LT = std::numeric_limits<double>::quiet_NaN();
			calculate_hxr_UA_1(ms_od_par.m_N_sub_hxrs, Q_dot_LT, m_dot_mc, m_dot_t, m_temp_od[2-cpp_offset], m_temp_od[8-cpp_offset], 
				m_pres_od[2-cpp_offset], m_pres_od[3-cpp_offset], m_pres_od[8-cpp_offset], m_pres_od[9-cpp_offset], hx_error_code, UA_LT_calc, min_DT_LT);

			if(hx_error_code > 0)
			{
				if(hx_error_code == 11)			// second-law violation in hxr, therefore temp(9) is too low
				{
					T9_lower_bound = m_temp_od[9-cpp_offset];
					m_temp_od[9-cpp_offset] = 0.5*(T9_lower_bound + T9_upper_bound);	// bisect bounds for next guess
					hx_error_code = 0;
					continue;				
				}
				else
				{
					error_code = hx_error_code;
					return;
				}
			}

			// Check for convergence and adjust T9 appropriately
			double UA_LT_residual = UA_LT - UA_LT_calc;

			if( fabs(UA_LT_residual) < 1.E-12 )		// catches no LT case
				break;

			double secant_guess = m_temp_od[9-cpp_offset] - UA_LT_residual*(last_T9_guess-m_temp_od[9-cpp_offset])/(last_LT_residual-UA_LT_residual);	// next guess predicted using secant method

			if(UA_LT_residual < 0.0)		// UA_LT_calc is too big, temp(9) needs to be higher
			{
				if( fabs(UA_LT_residual) / UA_LT < ms_od_par.m_tol )		// UA_LT converged (residual is negative
					break;
				T9_lower_bound = m_temp_od[9-cpp_offset];
			}
			else			// UA_LT_calc is too small, temp(9) needs to be lower
			{
				if( UA_LT_residual / UA_LT < ms_od_par.m_tol )				// UA_LT converged
					break;

				if( min_DT_LT < temperature_tolerance )			// UA_calc is still too low but there isn't anywhere to go so it's ok (catches huge UA values)
					break;

				T9_upper_bound = m_temp_od[9-cpp_offset];
			}

			last_LT_residual = UA_LT_residual;			// reset last stored residual value
			last_T9_guess = m_temp_od[9-cpp_offset];	// reset last stored guess value

			// Check if the secant method overshoots and fall abck to bisection if it does
			if( secant_guess <= T9_lower_bound || secant_guess >= T9_upper_bound || secant_guess != secant_guess )		// secant method overshot (or is NaN), use bisection
				m_temp_od[9-cpp_offset] = 0.5*(T9_lower_bound + T9_upper_bound);
			else
				m_temp_od[9-cpp_offset] = secant_guess;

		}	// End of T9 iteration

		// Check that T9_loop converged
		if( T9_iter >= max_iter )
		{
			error_code = 31;
			return;
		}

		// State 3 can now be fully defined
		m_enth_od[3-cpp_offset] = m_enth_od[2-cpp_offset] + Q_dot_LT/m_dot_mc;		// Energy balance on cold stream of low-temp recuperator
		prop_error_code = CO2_PH(m_pres_od[3-cpp_offset], m_enth_od[3-cpp_offset], &co2_props);
		if(prop_error_code != 0)
		{
			error_code = prop_error_code;
			return;
		}
		m_temp_od[3-cpp_offset] = co2_props.temp;
		m_entr_od[3-cpp_offset] = co2_props.entr;
		m_dens_od[3-cpp_offset] = co2_props.dens;

		// Go through mixing valve
		if( ms_od_par.m_recomp_frac >= 1.E-12 )
		{
			// Conservation of energy (both sides divided by m_dot_t)
			m_enth_od[4-cpp_offset] = (1.0 - ms_od_par.m_recomp_frac)*m_enth_od[3-cpp_offset] + ms_od_par.m_recomp_frac*m_enth_od[10-cpp_offset];	
			prop_error_code = CO2_PH(m_pres_od[4-cpp_offset], m_enth_od[4-cpp_offset], &co2_props);
			if(prop_error_code != 0)
			{
				error_code = prop_error_code;
				return;
			}
			m_temp_od[4-cpp_offset] = co2_props.temp;
			m_entr_od[4-cpp_offset] = co2_props.entr;
			m_dens_od[4-cpp_offset] = co2_props.dens;		
		}
		else	// no mixing valve, therefore state 4 is equal to state 3
		{
			m_temp_od[4-cpp_offset] = m_temp_od[3-cpp_offset];
			m_enth_od[4-cpp_offset] = m_enth_od[3-cpp_offset];
			m_entr_od[4-cpp_offset] = m_entr_od[3-cpp_offset];
			m_dens_od[4-cpp_offset] = m_dens_od[3-cpp_offset];
		}

		// Check for a second law violation at the outlet of the high-temp recuperator
		if(m_temp_od[4-cpp_offset] >= m_temp_od[8-cpp_offset])		// temp(8) is not valid and it must be increased
		{
			T8_lower_bound = m_temp_od[8-cpp_offset];
			m_temp_od[8-cpp_offset] = 0.5*(T8_lower_bound + T8_upper_bound);
			continue;
		}

		// Calculate the UA value of the high-temperature recuperator
		Q_dot_HT = std::numeric_limits<double>::quiet_NaN();
		if( UA_HT < 1.E-12 )		// no high-temp recuperator (this check is necessary to prevent pressure drops with UA=0 from causing problems)
			Q_dot_HT = 0.0;
		else
			Q_dot_HT = m_dot_t*(m_enth_od[7-cpp_offset] - m_enth_od[8-cpp_offset]);

		int HT_hx_error_code = 0;
		double min_DT_HT = std::numeric_limits<double>::quiet_NaN();
		calculate_hxr_UA_1(ms_od_par.m_N_sub_hxrs, Q_dot_HT, m_dot_t, m_dot_t, m_temp_od[4-cpp_offset], m_temp_od[7-cpp_offset], m_pres_od[4-cpp_offset], m_pres_od[5-cpp_offset],
			m_pres_od[7-cpp_offset], m_pres_od[8-cpp_offset], HT_hx_error_code, UA_HT_calc, min_DT_HT);

		if(HT_hx_error_code != 0)
		{
			if(HT_hx_error_code == 11)		// second-law violation in hxr, therefore temp(8) is too low
			{
				T8_lower_bound = m_temp_od[8-cpp_offset];
				m_temp_od[8-cpp_offset] = 0.5*(T8_lower_bound + T8_upper_bound);	// bisect bounds for next guess
				HT_hx_error_code = 0;
				continue;
			}
			else
			{
				error_code = HT_hx_error_code;
				return;
			}		
		}

		// Check for convergence and adjust T8 appropriately
		double UA_HT_residual = UA_HT - UA_HT_calc;

		if( fabs(UA_HT_residual) < 1.E-12 )			// catches no HT case
			break;

		double secant_guess = m_temp_od[8-cpp_offset] - UA_HT_residual*(last_T8_guess-m_temp_od[8-cpp_offset])/(last_HT_residual - UA_HT_residual);	// next guess predicted using secant method

		if(UA_HT_residual < 0.0)		// UA_HT_calc is too big, temp(8) needs to be higher
		{
			if( fabs(UA_HT_residual) / UA_HT < ms_od_par.m_tol )		// UA_HT converged (residual is negative)
				break;

			T8_lower_bound = m_temp_od[8-cpp_offset];
		}
		else		// UA_HT_calc is too small, temp(8) needs to be lower
		{
			if( UA_HT_residual / UA_HT < ms_od_par.m_tol )	// UA _HT converged
				break;

			if( min_DT_HT < temperature_tolerance )			// UA_calc is still too low, but there isn't anywhere to go so it's ok (catches huge UA values)
				break;

			T8_upper_bound = m_temp_od[8-cpp_offset];
		}

		last_HT_residual = UA_HT_residual;			// reset last stored residual value
		last_T8_guess = m_temp_od[8-cpp_offset];	// reset last stored guess value

		// Check if the secant method overshoots and fall back to bisection if it does
		if(secant_guess <= T8_lower_bound || secant_guess >= T8_upper_bound)
			m_temp_od[8-cpp_offset] = 0.5*(T8_lower_bound + T8_upper_bound);
		else
			m_temp_od[8-cpp_offset] = secant_guess;

	}	// End of T8 iteration

	// Check that T8_loop converged
	if(T8_iter >= max_iter)
	{
		error_code = 35;
		return;
	}

	// State 5 can now be fully defined
	m_enth_od[5-cpp_offset] = m_enth_od[4-cpp_offset] + Q_dot_HT/m_dot_t;		//[kJ/kg] Energy balance on cold stream of high-temp recuperator
	prop_error_code = CO2_PH(m_pres_od[5-cpp_offset], m_enth_od[5-cpp_offset], &co2_props);
	if(prop_error_code != 0)
	{
		error_code = prop_error_code;
		return;
	}
	m_temp_od[5-cpp_offset] = co2_props.temp;
	m_entr_od[5-cpp_offset] = co2_props.entr;
	m_dens_od[5-cpp_offset] = co2_props.dens;

	// Calculate cycle performance metrics
	double w_mc = m_enth_od[1-cpp_offset] - m_enth_od[2-cpp_offset];		//[kJ/kg] (negative) specific work of compressor
	double w_t = m_enth_od[6-cpp_offset] - m_enth_od[7-cpp_offset];			//[kJ/kg] (positive) specific work of turbine

	double w_rc = 0.0;
	if(ms_od_par.m_recomp_frac > 0.0)
		w_rc = m_enth_od[9-cpp_offset] - m_enth_od[10-cpp_offset];			//[kJ/kg] (negative) specific work of recompressor


	m_Q_dot_PHX_od = m_dot_t*(m_enth_od[6-cpp_offset] - m_enth_od[5-cpp_offset]);
	m_W_dot_net_od = w_mc*m_dot_mc + w_rc*m_dot_rc + w_t*m_dot_t;			
	m_eta_thermal_od = m_W_dot_net_od / m_Q_dot_PHX_od;

	// Set ms_od_solved
	ms_od_solved.m_eta_thermal = m_eta_thermal_od;
	ms_od_solved.m_W_dot_net = m_W_dot_net_od;
	ms_od_solved.m_Q_dot = m_Q_dot_PHX_od;
	ms_od_solved.m_m_dot_mc = m_dot_mc;
	ms_od_solved.m_m_dot_rc = m_dot_rc;
	ms_od_solved.m_m_dot_t = m_dot_t;
	ms_od_solved.m_recomp_frac = ms_od_par.m_recomp_frac;
	ms_od_solved.m_N_mc = ms_od_par.m_N_mc;
	ms_od_solved.m_N_t = ms_od_par.m_N_t;

	ms_od_solved.m_temp = m_temp_od;
	ms_od_solved.m_pres = m_pres_od;
	ms_od_solved.m_enth = m_enth_od;
	ms_od_solved.m_entr = m_entr_od;
	ms_od_solved.m_dens = m_dens_od;

	return;
}

void C_RecompCycle::target_off_design(S_target_od_parameters & tar_od_par_in, int & error_code)
{
	ms_tar_od_par = tar_od_par_in;

	int tar_od_error_code = 0;

	target_off_design_core(tar_od_error_code);

	error_code = tar_od_error_code;
}

void C_RecompCycle::target_off_design_core(int & error_code)
{
	int max_iter = 100;

	// 10.6.14 twn: Increase from 20 to 40 to hopefully improve convergence when q_target is close (but less than) q_max
	int search_intervals; 
	if( ms_tar_od_par.m_use_default_res )
		search_intervals = 20;
	else
		search_intervals = 50;		// number of intervals to check for valid bounds before starting secant loop

	// Determine the interval containing the solution
	bool lower_bound_found = false;
	bool upper_bound_found = false;

	double left_residual = -1.E12;		// Initialized to large negative value
	double right_residual = 1.E12;		// Initialized to large positive value

	double P_low = ms_tar_od_par.m_lowest_pressure;
	//double P_high = ms_tar_od_par.m_highest_pressure;
	double P_high = min(ms_tar_od_par.m_highest_pressure, 12000.0);

	std::vector<double> P_guesses(search_intervals+1);
	for( int i = 0; i <= search_intervals; i++ )
		P_guesses[i] = P_low + i*(P_high-P_low)/double(search_intervals);

	double biggest_value = 0.0;
	double biggest_cycle = 0.0;			// Track pressure instead of 'cycle'

	// Set 'ms_od_par' that are known
	ms_od_par.m_T_mc_in = ms_tar_od_par.m_T_mc_in;
	ms_od_par.m_T_t_in = ms_tar_od_par.m_T_t_in;
	ms_od_par.m_recomp_frac = ms_tar_od_par.m_recomp_frac;
	ms_od_par.m_N_mc = ms_tar_od_par.m_N_mc;
	ms_od_par.m_N_t = ms_tar_od_par.m_N_t;
	ms_od_par.m_N_sub_hxrs = ms_tar_od_par.m_N_sub_hxrs;
	ms_od_par.m_tol = ms_tar_od_par.m_tol;

	for( int i = 0; i <= search_intervals; i++ )
	{
		double P_guess = P_guesses[i];

		ms_od_par.m_P_mc_in = P_guess;

		int od_error_code = 0;
		off_design_core(od_error_code);

		if(od_error_code == 0)
		{
			if( m_pres_last[2 - 1] > ms_des_par.m_P_high_limit*1.2 )
				break;		// Compressor inlet pressure is getting too big

			double target_value = -999.9;
			if( ms_tar_od_par.m_is_target_Q )
				target_value = m_Q_dot_PHX_od;
			else
				target_value = m_W_dot_net_od;

			double residual = target_value - ms_tar_od_par.m_target;

			if(target_value > biggest_value)		// keep track of the largest value seen
			{
				biggest_value = target_value;
				biggest_cycle = P_guess;
			}

			if( residual >= 0.0 )					// value is above target
			{
				if( residual < right_residual )	// first rightbound or a better bound, use it
				{
					P_high = P_guess;
					right_residual = residual;
					upper_bound_found = true;
				}
			}
			else									// value is below target
			{
				if( residual > left_residual )	// note: residual and left_residual are negative
				{
					P_low = P_guess;
					left_residual = residual;
					lower_bound_found = true;
				}
			}		
		}		// End od_error_code = 0 loop
		if( lower_bound_found && upper_bound_found )
			break;
	}

	if( !lower_bound_found || !upper_bound_found )
	{
		error_code = 26;
		// return biggest cycle??
		return;
	}

	// Enter secant / bisection loop
	double P_guess = (P_low + P_high)*0.5;		// start with bisection (note: could use left and right bounds and residuals to get a better first guess)

	double last_P_guess = 1.E12;				// twn: set to some large value here???
	double last_residual = 1.23;				// twn: set to some small value here???

	int iter = 0;
	for( iter = 1; iter <= max_iter; iter++ )
	{
		ms_od_par.m_P_mc_in = P_guess;

		int od_error_code = 0;
		off_design_core(od_error_code);
		
		if( od_error_code != 0 )			// results not valid; choose a random value between P_low and P_high for next guess
		{
			double P_frac = rand() / (double)(RAND_MAX);
			P_guess = P_low + (P_high - P_low)*P_guess;
			continue;
		}

		// Check residual
		double residual = std::numeric_limits<double>::quiet_NaN();
		if( ms_tar_od_par.m_is_target_Q )
			residual = m_Q_dot_PHX_od - ms_tar_od_par.m_target;
		else
			residual = m_W_dot_net_od - ms_tar_od_par.m_target;
	
		if( residual >= 0.0 )		// value is above target
		{
			if( residual / ms_tar_od_par.m_target <= ms_tar_od_par.m_tol )		// converged
				break;
			P_high = P_guess;
		}
		else						// value is below target
		{
			if( -residual / ms_tar_od_par.m_target <= ms_tar_od_par.m_tol )
				break;
			P_low = P_guess;
		}

		if( fabs(P_high - P_low) < 0.1 )		// Interval is tiny; consider it converged
			break;

		// Determine next guess
		double P_secant = P_guess - residual*(last_P_guess-P_guess)/(last_residual-residual);		// next guess predicted using secant method
		last_P_guess = P_guess;
		last_residual = residual;
		P_guess = P_secant;

		if( P_guess <= P_low || P_guess >= P_high )
			P_guess = (P_low + P_high)*0.5;				// Secant overshot, use bisection

	}

	// Check for convergence
	if( iter >= max_iter )
	{
		error_code = 82;
		return;
	}

}

void C_RecompCycle::optimal_off_design(S_opt_od_parameters & opt_od_par_in, int & error_code)
{
	ms_opt_od_par = opt_od_par_in;

	int opt_od_error_code = 0;

	optimal_off_design_core(opt_od_error_code);

	error_code = opt_od_error_code;
}

void C_RecompCycle::optimal_off_design_core(int & error_code)
{
	// Set known values for ms_od_par
	ms_od_par.m_T_mc_in = ms_opt_od_par.m_T_mc_in;
	ms_od_par.m_T_t_in = ms_opt_od_par.m_T_t_in;
	ms_od_par.m_N_sub_hxrs = ms_opt_od_par.m_N_sub_hxrs;
	ms_od_par.m_tol = ms_opt_od_par.m_tol;

	// Initialize guess array
	int index = 0;

	std::vector<double> x(0);
	std::vector<double> lb(0);
	std::vector<double> ub(0);
	std::vector<double> scale(0);

	if( !ms_opt_od_par.m_fixed_P_mc_in )
	{
		x.push_back(ms_opt_od_par.m_P_mc_in_guess);
		lb.push_back(100.0);
		ub.push_back(ms_des_par.m_P_high_limit);
		//scale.push_back(0.25*ms_opt_od_par.m_P_mc_in_guess);
		scale.push_back(50.0);

		index++;
	}

	if( !ms_opt_od_par.m_fixed_recomp_frac )
	{
		x.push_back(ms_opt_od_par.m_recomp_frac_guess);
		lb.push_back(0.0);
		ub.push_back(1.0);
		scale.push_back(0.05);

		index++;
	}

	if( !ms_opt_od_par.m_fixed_N_mc )
	{
		x.push_back(ms_opt_od_par.m_N_mc_guess);
		lb.push_back(1.0);
		ub.push_back(HUGE_VAL);
		scale.push_back(0.25*ms_opt_od_par.m_N_mc_guess);
		//scale.push_back(100.0);

		index++;
	}

	if( !ms_opt_od_par.m_fixed_N_t )
	{
		x.push_back(ms_opt_od_par.m_N_t_guess);
		lb.push_back(1.0);
		ub.push_back(HUGE_VAL);
		scale.push_back(100.0);

		index++;
	}

	m_W_dot_net_max = 0.0;
	bool solution_found = false;
	if(index > 0)		// need to call subplex
	{
		// Set up instance of nlopt class and set optimization parameters
		nlopt::opt		opt_od_cycle(nlopt::LN_SBPLX, index);
		opt_od_cycle.set_lower_bounds(lb);
		opt_od_cycle.set_upper_bounds(ub);
		opt_od_cycle.set_initial_step(scale);
		opt_od_cycle.set_xtol_rel(ms_opt_od_par.m_opt_tol);

		// Set max objective function
		opt_od_cycle.set_max_objective(nlopt_cb_opt_od, this);
		double max_f = std::numeric_limits<double>::quiet_NaN();
		nlopt::result   result_od_cycle = opt_od_cycle.optimize(x, max_f);

		int opt_od_error_code = 0;
		if(m_W_dot_net_max > 0.0)
		{
			ms_od_par = ms_od_par_optimal;
			off_design_core(opt_od_error_code);
			error_code = 0;
		}
		else
		{
			error_code = 111;
			return;
		}

		if( opt_od_error_code != 0 )
		{
			error_code = opt_od_error_code;
			return;
		}

	}
	else		// Just call off design subroutine (with fixed inputs)
	{
		double blah = 1.23;
	}

}

double C_RecompCycle::off_design_point_value(const std::vector<double> &x)
{
	// 'x' is array of inputs either being adjusted by optimizer or set constant
	// Finish defining 'ms_od_par' based on current 'x' values

	int index = 0;

	if( !ms_opt_od_par.m_fixed_P_mc_in )
	{
		ms_od_par.m_P_mc_in = x[index];
		index++;
	}
	else
		ms_od_par.m_P_mc_in = ms_opt_od_par.m_P_mc_in_guess;

	if( !ms_opt_od_par.m_fixed_recomp_frac )
	{
		ms_od_par.m_recomp_frac = x[index];
		index++;
	}
	else
		ms_od_par.m_recomp_frac = ms_opt_od_par.m_recomp_frac_guess;

	if( !ms_opt_od_par.m_fixed_N_mc )
	{
		ms_od_par.m_N_mc = x[index];
		index++;
	}
	else
		ms_od_par.m_N_mc = ms_opt_od_par.m_N_mc_guess;

	if( !ms_opt_od_par.m_fixed_N_t )
	{
		ms_od_par.m_N_t = x[index];
		index++;
	}
	else
		ms_od_par.m_N_t = ms_opt_od_par.m_N_t_guess;

	if( ms_od_par.m_N_t <= 0.0 )
		ms_od_par.m_N_t = ms_od_par.m_N_mc;		// link turbine and main compressor shafts

	// Check inputs
	if(ms_od_par.m_recomp_frac < 0.0)
	{
		return 0.0;
	}

	// Call off_design subroutine
		// 'ms_od_par' has been defined here and in 'optimal_off_design_core'
	int od_error_code = 0;
	off_design_core(od_error_code);

	if( od_error_code != 0 )
		return 0.0;

	double off_design_point_value = 0.0;
	if( ms_opt_od_par.m_is_max_W_dot )
		off_design_point_value = m_W_dot_net_od;
	else
		off_design_point_value = m_eta_thermal_od;

	// Hardcode some compressor checks to 'true', per John's code. Could revisit later
	bool surge_allowed = true;
	bool supersonic_tip_speed_allowed = true;
	
	// Check validity
	if( m_pres_od[2 - 1] > ms_des_par.m_P_high_limit )		// above high-pressure limit; provide optimizer with more information
	{
		//off_design_point_value = max(1.0, off_design_point_value / (10.0 + m_pres_od[2 - 1] - ms_des_par.m_P_high_limit, 4.0));
		double penalty = 5.0;
		//off_design_point_value = off_design_point_value * (1.0 - max(0.0, 1.0 - penalty*(ms_des_par.m_P_high_limit / m_pres_od[2 - 1])));
		off_design_point_value = off_design_point_value*(1.0 - penalty*max(0.0,(m_pres_od[2-1] - ms_des_par.m_P_high_limit)/ms_des_par.m_P_high_limit));
	}

	if(!surge_allowed)		// twn: Note that 'surge_allowed' is currently hardcoded to true so this won't be executed
	{
		if( m_mc.get_od_solved()->m_surge )
			off_design_point_value = 0.0;
		
		if( ms_od_par.m_recomp_frac > 0.0 && m_rc.get_od_solved()->m_surge )
			off_design_point_value = 0.0;
	}

	if(!supersonic_tip_speed_allowed)
	{
		double penalty = 5.0;

		if( m_mc.get_od_solved()->m_w_tip_ratio > 1.0 )
			off_design_point_value = fabs(off_design_point_value)*(1.0 - penalty*max(0.0, m_mc.get_od_solved()->m_w_tip_ratio - 1.0));

		if( ms_od_par.m_recomp_frac > 0.0 && m_rc.get_od_solved()->m_w_tip_ratio > 1.0 )
			off_design_point_value = fabs(off_design_point_value)*(1.0 - penalty*max(0.0, m_rc.get_od_solved()->m_w_tip_ratio - 1.0));

		if( m_t.get_od_solved()->m_w_tip_ratio > 1.0 )
			off_design_point_value = fabs(off_design_point_value)*(1.0 - penalty*max(0.0, m_t.get_od_solved()->m_w_tip_ratio - 1.0));
	}

	// Check if this is the optimal cycle?
	if(off_design_point_value > m_W_dot_net_max)
	{
		ms_od_par_optimal = ms_od_par;
		m_W_dot_net_max = off_design_point_value;
	}

	return off_design_point_value;

}

void C_RecompCycle::get_max_output_od(S_opt_target_od_parameters & opt_tar_od_par_in, int & error_code)
{
	ms_opt_tar_od_par = opt_tar_od_par_in;

	// Determine the largest possible power output of the cycle
	bool point_found = false;
	double P_low = ms_opt_tar_od_par.m_lowest_pressure;

	// Define known members of 'ms_opt_od_par' from 'ms_opt_tar_od_par'
	ms_opt_od_par.m_T_mc_in = ms_opt_tar_od_par.m_T_mc_in;
	ms_opt_od_par.m_T_t_in = ms_opt_tar_od_par.m_T_t_in;
	// .m_is_max_W_dot   --- need to define in loop
	ms_opt_od_par.m_N_sub_hxrs = ms_opt_tar_od_par.m_N_sub_hxrs;
	// m_P_mc_in_guess   --- need to define in loop
	// m_fixed_P_mc_in   --- should be 'false', but define in loop
	ms_opt_od_par.m_recomp_frac_guess = ms_opt_tar_od_par.m_recomp_frac_guess;
	ms_opt_od_par.m_fixed_recomp_frac = ms_opt_tar_od_par.m_fixed_recomp_frac;

	ms_opt_od_par.m_N_mc_guess = ms_opt_tar_od_par.m_N_mc_guess*1.25;		// twn: Start with assuming at max power the compressor speed will be greater than design
	ms_opt_od_par.m_fixed_N_mc = ms_opt_tar_od_par.m_fixed_N_mc;

	ms_opt_od_par.m_N_t_guess = ms_opt_tar_od_par.m_N_t_guess;
	ms_opt_od_par.m_fixed_N_t = ms_opt_tar_od_par.m_fixed_N_t;

	ms_opt_od_par.m_tol = ms_opt_tar_od_par.m_tol;
	ms_opt_od_par.m_opt_tol = ms_opt_tar_od_par.m_opt_tol;

	do
	{
		ms_opt_od_par.m_is_max_W_dot = true;
		ms_opt_od_par.m_P_mc_in_guess = P_low;
		ms_opt_od_par.m_fixed_P_mc_in = false;

		// Try fixing inlet pressure and see if f_recomp and N_mc are modified
		// ms_opt_od_par.m_P_mc_in_guess = 8700.0;
		// ms_opt_od_par.m_fixed_P_mc_in = true;

		int od_error_code = 0;

		optimal_off_design_core(od_error_code);

		if( od_error_code == 0 )
		{

			// Update guess parameters
			ms_opt_od_par.m_recomp_frac_guess = ms_od_par.m_recomp_frac;
			ms_opt_od_par.m_N_mc_guess = ms_od_par.m_N_mc;
			ms_opt_od_par.m_N_t_guess = ms_od_par.m_N_t;
			ms_opt_od_par.m_P_mc_in_guess = ms_od_par.m_P_mc_in;
			P_low = ms_od_par.m_P_mc_in;

			if( point_found )		// exit only after testing two starting points (prevents optimization near-misses)
				break;

			point_found = true;
		}
		else
		{
			P_low = 1.1*P_low;
		}
		

		if( P_low > ms_opt_tar_od_par.m_highest_pressure )
			break;

	} while( true );

	m_biggest_target = -999.9;

	if( !point_found )
	{
		error_code = 99;
		return;
	}

	if( ms_opt_tar_od_par.m_is_target_Q )
		m_biggest_target = m_Q_dot_PHX_od;
	else
		m_biggest_target = m_W_dot_net_od;

}

void C_RecompCycle::optimal_target_off_design_no_check(S_opt_target_od_parameters & opt_tar_od_par_in, int & error_code)
{
	ms_opt_tar_od_par = opt_tar_od_par_in;

	// Populate 'ms_tar_od_par' from info in 'ms_opt_tar_od_par'
	ms_tar_od_par.m_T_mc_in = ms_opt_tar_od_par.m_T_mc_in;
	ms_tar_od_par.m_T_t_in = ms_opt_tar_od_par.m_T_t_in;
	// ms_tar_od_par.m_recomp_frac ... Defined by optimizer
	// ms_tar_od_par.m_N_mc ... Defined by optimizer
	// ms_tar_od_par.m_N_t  ... Defined by optimizer
	ms_tar_od_par.m_N_sub_hxrs = ms_opt_tar_od_par.m_N_sub_hxrs;
	ms_tar_od_par.m_tol = ms_opt_tar_od_par.m_tol;
	ms_tar_od_par.m_target = ms_opt_tar_od_par.m_target;
	ms_tar_od_par.m_is_target_Q = ms_opt_tar_od_par.m_is_target_Q;
	ms_tar_od_par.m_lowest_pressure = ms_opt_tar_od_par.m_lowest_pressure;
	ms_tar_od_par.m_highest_pressure = ms_opt_tar_od_par.m_highest_pressure;

	ms_tar_od_par.m_use_default_res = ms_opt_tar_od_par.m_use_default_res;

	// Initialize guess array
	int index = 0;

	std::vector<double> x(0);
	std::vector<double> lb(0);
	std::vector<double> ub(0);
	std::vector<double> scale(0);

	if( !ms_opt_tar_od_par.m_fixed_recomp_frac )
	{
		x.push_back(ms_opt_tar_od_par.m_recomp_frac_guess);
		lb.push_back(0.0);
		ub.push_back(1.0);
		scale.push_back(0.01);

		index++;
	}

	if( !ms_opt_tar_od_par.m_fixed_N_mc )
	{
		x.push_back(ms_opt_tar_od_par.m_N_mc_guess);
		lb.push_back(1.0);
		ub.push_back(HUGE_VAL);
		scale.push_back(0.25*ms_opt_tar_od_par.m_N_mc_guess);

		index++;
	}

	if( !ms_opt_tar_od_par.m_fixed_N_t )
	{
		x.push_back(ms_opt_tar_od_par.m_N_t_guess);
		lb.push_back(1.0);
		ub.push_back(HUGE_VAL);
		scale.push_back(100.0);
	}

	bool solution_found = false;
	m_eta_best = 0.0;

	if( index > 0 )
	{
		// Set up instance of nlopt class and set optimization parameters
		nlopt::opt		opt_tar_od_cycle(nlopt::LN_SBPLX, index);
		opt_tar_od_cycle.set_lower_bounds(lb);
		opt_tar_od_cycle.set_upper_bounds(ub);
		opt_tar_od_cycle.set_initial_step(scale);
		opt_tar_od_cycle.set_xtol_rel(ms_opt_tar_od_par.m_opt_tol);

		// Set max objective function
		opt_tar_od_cycle.set_max_objective(nlopt_cb_eta_at_target, this);
		double max_f = std::numeric_limits<double>::quiet_NaN();
		nlopt::result     result_tar_od_cycle = opt_tar_od_cycle.optimize(x, max_f);
	}
	else
	{
		eta_at_target(x);
	}

	// Final call to off-design model using 'ms_od_par_tar_optimal'
	int od_error_code = 0;
	if( m_eta_best > 0.0 )
	{
		ms_od_par = ms_od_par_tar_optimal;
		off_design_core(od_error_code);
		error_code = 0;
	}
	else
	{
		error_code = 98;
		return;
	}

	if( od_error_code != 0 )
	{
		error_code = od_error_code;
		return;
	}
}

void C_RecompCycle::optimal_target_off_design(S_opt_target_od_parameters & opt_tar_od_par_in, int & error_code)
{
	int error_code_local = 0;

	get_max_output_od(opt_tar_od_par_in, error_code_local);

	if(error_code_local != 0)
	{
		error_code = error_code_local;
		return;
	}

	// If the target is not possible, return the cycle with the largest (based on power output)
	if( m_biggest_target < ms_opt_tar_od_par.m_target )
	{
		error_code = 123;
		return;
	}

	optimal_target_off_design_no_check(opt_tar_od_par_in, error_code_local);

	if(error_code_local != 0)
	{
		error_code = error_code_local;
		return;
	}

	return;

	/*
	ms_opt_tar_od_par = opt_tar_od_par_in;

	// Determine the largest possible power output of the cycle
	bool point_found = false;
	double P_low = ms_opt_tar_od_par.m_lowest_pressure;

	// Define known members of 'ms_opt_od_par' from 'ms_opt_tar_od_par'
	ms_opt_od_par.m_T_mc_in = ms_opt_tar_od_par.m_T_mc_in;
	ms_opt_od_par.m_T_t_in = ms_opt_tar_od_par.m_T_t_in;
		// .m_is_max_W_dot   --- need to define in loop
	ms_opt_od_par.m_N_sub_hxrs = ms_opt_tar_od_par.m_N_sub_hxrs;
		// m_P_mc_in_guess   --- need to define in loop
		// m_fixed_P_mc_in   --- should be 'false', but define in loop
	ms_opt_od_par.m_recomp_frac_guess = ms_opt_tar_od_par.m_recomp_frac_guess;
	ms_opt_od_par.m_fixed_recomp_frac = ms_opt_tar_od_par.m_fixed_recomp_frac;

	ms_opt_od_par.m_N_mc_guess = ms_opt_tar_od_par.m_N_mc_guess*1.25;		// twn: Start with assuming at max power the compressor speed will be greater than design
	ms_opt_od_par.m_fixed_N_mc = ms_opt_tar_od_par.m_fixed_N_mc;

	ms_opt_od_par.m_N_t_guess = ms_opt_tar_od_par.m_N_t_guess;
	ms_opt_od_par.m_fixed_N_t = ms_opt_tar_od_par.m_fixed_N_t;

	ms_opt_od_par.m_tol = ms_opt_tar_od_par.m_tol;
	ms_opt_od_par.m_opt_tol = ms_opt_tar_od_par.m_opt_tol;

	do
	{
		ms_opt_od_par.m_is_max_W_dot = true;
		ms_opt_od_par.m_P_mc_in_guess = P_low;
		ms_opt_od_par.m_fixed_P_mc_in = false;

		// Try fixing inlet pressure and see if f_recomp and N_mc are modified
		// ms_opt_od_par.m_P_mc_in_guess = 8700.0;
		// ms_opt_od_par.m_fixed_P_mc_in = true;

		int od_error_code = 0;

		optimal_off_design_core(od_error_code);

		if(od_error_code == 0)
		{									

			// Update guess parameters
			ms_opt_od_par.m_recomp_frac_guess = ms_od_par.m_recomp_frac;
			ms_opt_od_par.m_N_mc_guess = ms_od_par.m_N_mc;
			ms_opt_od_par.m_N_t_guess = ms_od_par.m_N_t;
			ms_opt_od_par.m_P_mc_in_guess = ms_od_par.m_P_mc_in;

			if( point_found )		// exit only after testing two starting points (prevents optimization near-misses)
				break;

			point_found = true; 
		}

		P_low = 1.1*ms_opt_od_par.m_P_mc_in_guess;

		if( P_low > ms_opt_tar_od_par.m_highest_pressure )
			break;

	} while( true );

	m_biggest_target = -999.9;

	if( !point_found )
	{
		error_code = 99;
		return;
	}

	if( ms_opt_tar_od_par.m_is_target_Q )
		m_biggest_target = m_Q_dot_PHX_od;
	else
		m_biggest_target = m_W_dot_net_od;

	// If the target is not possible, return the cycle with the largest (based on power output)
	if( m_biggest_target < ms_opt_tar_od_par.m_target )
	{
		error_code = 123;
		return;
	}

	// Populate 'ms_tar_od_par' from info in 'ms_opt_tar_od_par'
	ms_tar_od_par.m_T_mc_in = ms_opt_tar_od_par.m_T_mc_in;
	ms_tar_od_par.m_T_t_in = ms_opt_tar_od_par.m_T_t_in;
		// ms_tar_od_par.m_recomp_frac ... Defined by optimizer
		// ms_tar_od_par.m_N_mc ... Defined by optimizer
		// ms_tar_od_par.m_N_t  ... Defined by optimizer
	ms_tar_od_par.m_N_sub_hxrs = ms_opt_tar_od_par.m_N_sub_hxrs;
	ms_tar_od_par.m_tol = ms_opt_tar_od_par.m_tol;
	ms_tar_od_par.m_target = ms_opt_tar_od_par.m_target;
	ms_tar_od_par.m_is_target_Q = ms_opt_tar_od_par.m_is_target_Q;
	ms_tar_od_par.m_lowest_pressure = ms_opt_tar_od_par.m_lowest_pressure;
	ms_tar_od_par.m_highest_pressure = ms_opt_tar_od_par.m_highest_pressure;

	// Initialize guess array
	int index = 0;

	std::vector<double> x(0);
	std::vector<double> lb(0);
	std::vector<double> ub(0);
	std::vector<double> scale(0);

	if(!ms_opt_tar_od_par.m_fixed_recomp_frac)
	{
		x.push_back(ms_opt_tar_od_par.m_recomp_frac_guess);
		lb.push_back(0.0);
		ub.push_back(1.0);
		scale.push_back(0.01);

		index++;
	}

	if(!ms_opt_tar_od_par.m_fixed_N_mc)
	{
		x.push_back(ms_opt_tar_od_par.m_N_mc_guess);
		lb.push_back(1.0);
		ub.push_back(HUGE_VAL);
		scale.push_back(0.25*ms_opt_tar_od_par.m_N_mc_guess);

		index++;
	}

	if(!ms_opt_tar_od_par.m_fixed_N_t)
	{
		x.push_back(ms_opt_tar_od_par.m_N_t_guess);
		lb.push_back(1.0);
		ub.push_back(HUGE_VAL);
		scale.push_back(100.0);
	}

	bool solution_found = false;
	m_eta_best = 0.0;

	if(index > 0)
	{
		// Set up instance of nlopt class and set optimization parameters
		nlopt::opt		opt_tar_od_cycle(nlopt::LN_SBPLX, index);
		opt_tar_od_cycle.set_lower_bounds(lb);
		opt_tar_od_cycle.set_upper_bounds(ub);
		opt_tar_od_cycle.set_initial_step(scale);
		opt_tar_od_cycle.set_xtol_rel(ms_opt_tar_od_par.m_opt_tol);

		// Set max objective function
		opt_tar_od_cycle.set_max_objective(nlopt_cb_eta_at_target, this);
		double max_f = std::numeric_limits<double>::quiet_NaN();
		nlopt::result     result_tar_od_cycle = opt_tar_od_cycle.optimize(x, max_f);
	}
	else
	{
		eta_at_target(x);
	}

	// Final call to off-design model using 'ms_od_par_tar_optimal'
	int od_error_code = 0;
	if(m_eta_best > 0.0)
	{
		ms_od_par = ms_od_par_tar_optimal;
		off_design_core(od_error_code);
		error_code = 0;
	}
	else
	{
		error_code = 98;
		return;
	}

	if(od_error_code != 0)
	{
		error_code = od_error_code;
		return;
	}
	*/
}

double C_RecompCycle::eta_at_target(const std::vector<double> &x)
{
	// Get input variables from 'x'
	int index = 0;

	if( !ms_opt_tar_od_par.m_fixed_recomp_frac )
	{
		ms_tar_od_par.m_recomp_frac = x[index];
		index++;
	}
	else
		ms_tar_od_par.m_recomp_frac = ms_opt_tar_od_par.m_recomp_frac_guess;

	if( !ms_opt_tar_od_par.m_fixed_N_mc )
	{
		ms_tar_od_par.m_N_mc = x[index];
		index++;
	}
	else
		ms_tar_od_par.m_N_mc = ms_opt_tar_od_par.m_N_mc_guess;

	if( !ms_opt_tar_od_par.m_fixed_N_t )
	{
		ms_tar_od_par.m_N_t = x[index];
		index++;
	}
	else
		ms_tar_od_par.m_N_t = ms_opt_tar_od_par.m_N_t_guess;

	if( ms_tar_od_par.m_N_t <= 0.0 )
		ms_tar_od_par.m_N_t = ms_tar_od_par.m_N_mc;

	// Check inputs
	if( ms_tar_od_par.m_recomp_frac < 0.0 )
	{
		return 0.0;
	}

	int target_od_error_code = 0;

	// Call target_off_design subroutine
	target_off_design_core(target_od_error_code);

	double eta_at_target = std::numeric_limits<double>::quiet_NaN();
	if(target_od_error_code==26)
	{
		//return 1.0 / (100.0 + m_W_dot_net_od);
		return 0.0;
	}
	else if(target_od_error_code != 0)
	{
		return 0.0;
	}
	else
		eta_at_target = m_eta_thermal_od;

	// Check validity
	if( m_pres_od[2 - 1] > ms_des_par.m_P_high_limit )		// above high-pressure limit; provide optimizer with more information
	{
		//off_design_point_value = max(1.0, off_design_point_value / (10.0 + m_pres_od[2 - 1] - ms_des_par.m_P_high_limit, 4.0));
		double penalty = 5.0;
		//off_design_point_value = off_design_point_value * (1.0 - max(0.0, 1.0 - penalty*(ms_des_par.m_P_high_limit / m_pres_od[2 - 1])));
		eta_at_target = eta_at_target*(1.0 - penalty*max(0.0, (m_pres_od[2 - 1] - ms_des_par.m_P_high_limit) / ms_des_par.m_P_high_limit));
	}

	// Hardcode some compressor checks to 'true', per John's code. Could revisit later
	bool surge_allowed = true;
	bool supersonic_tip_speed_allowed = true;

	if( !surge_allowed )		// twn: Note that 'surge_allowed' is currently hardcoded to true so this won't be executed
	{
		if( m_mc.get_od_solved()->m_surge )
			eta_at_target = 0.0;

		if( ms_od_par.m_recomp_frac > 0.0 && m_rc.get_od_solved()->m_surge )
			eta_at_target = 0.0;
	}

	if( !supersonic_tip_speed_allowed )
	{
		double penalty = 5.0;

		if( m_mc.get_od_solved()->m_w_tip_ratio > 1.0 )
			eta_at_target = fabs(eta_at_target)*(1.0 - penalty*max(0.0, m_mc.get_od_solved()->m_w_tip_ratio - 1.0));

		if( ms_od_par.m_recomp_frac > 0.0 && m_rc.get_od_solved()->m_w_tip_ratio > 1.0 )
			eta_at_target = fabs(eta_at_target)*(1.0 - penalty*max(0.0, m_rc.get_od_solved()->m_w_tip_ratio - 1.0));

		if( m_t.get_od_solved()->m_w_tip_ratio > 1.0 )
			eta_at_target = fabs(eta_at_target)*(1.0 - penalty*max(0.0, m_t.get_od_solved()->m_w_tip_ratio - 1.0));
	}

	// Check if this is the optimal cycle?
	if( eta_at_target > m_eta_best)
	{
		ms_od_par_tar_optimal = ms_od_par;
		m_eta_best = eta_at_target;
	}

	return eta_at_target;
}

void C_RecompCycle::opt_od_eta_for_hx(S_od_parameters & od_par_in, S_PHX_od_parameters phx_od_par_in, int & error_code)
{
	ms_od_par = od_par_in;
	ms_phx_od_par = phx_od_par_in;

	// Set up 3-D optimization in NLOPT
	std::vector<double> x(0);
	std::vector<double> lb(0);
	std::vector<double> ub(0);
	std::vector<double> scale(0);
	int index = 0;

	// Inlet pressure
	//x.push_back(ms_des_solved.m_pres[1 - 1]);
	x.push_back(1000.0);
	lb.push_back(1000.0);		// This must be set to a variable somewhere?
	ub.push_back(17000.0);		// This also must be set somewhere?
	scale.push_back(4000.0);	// Solution is probably less than design pressure
	index++;

	// Recompression Fraction
	if( ms_des_solved.m_is_rc )
	{
		x.push_back(ms_des_solved.m_recomp_frac);
		lb.push_back(0.0);
		ub.push_back(1.0);
		scale.push_back(-.02);
		index++;
	}

	m_found_opt = false;
	m_eta_phx_max = 0.0;

	// Compressor Speed
	x.push_back(ms_des_solved.m_N_mc);
	lb.push_back(ms_des_solved.m_N_mc*0.1);
	ub.push_back(ms_des_solved.m_N_mc*1.5);
	scale.push_back(ms_des_solved.m_N_mc*0.1);
	index++;

	// Save initial vectors
	std::vector<double> x_base = x;
	std::vector<double> lb_base = lb;
	std::vector<double> ub_base = ub;
	std::vector<double> sc_base = scale;

	// Set up instance of nlopt class and set optimization parameters
	nlopt::opt          opt_od_cycle(nlopt::LN_SBPLX, index);
	opt_od_cycle.set_lower_bounds(lb);
	opt_od_cycle.set_upper_bounds(ub);
	opt_od_cycle.set_initial_step(scale);
	opt_od_cycle.set_xtol_rel(ms_des_par.m_tol);

	// Set max objective function
	opt_od_cycle.set_max_objective(nlopt_cb_opt_od_eta, this);
	double max_f = std::numeric_limits<double>::quiet_NaN();
	nlopt::result       result_od_cycle = opt_od_cycle.optimize(x, max_f);

	int od_error_code = 0;

	if( !m_found_opt )
	{
		x = x_base;
		lb = lb_base;
		ub = ub_base;
		scale = sc_base;

		x[index-1] = ms_des_solved.m_N_mc*1.5;
		lb[index-1] = ms_des_solved.m_N_mc*0.5;
		ub[index-1] = ms_des_solved.m_N_mc*1.75;
		scale[index-1] = -ms_des_solved.m_N_mc*0.1;
		
		opt_od_cycle.set_lower_bounds(lb);
		opt_od_cycle.set_upper_bounds(ub);
		opt_od_cycle.set_initial_step(scale);

		max_f = 0.0;

		result_od_cycle = opt_od_cycle.optimize(x, max_f);

		if( !m_found_opt )
		{
			od_error_code = 1;
		}
	}

	index = 0;
	ms_od_par.m_P_mc_in = x[index];
	index++;

	ms_od_par.m_recomp_frac = 0.0;
	if( ms_des_solved.m_is_rc )
	{
		ms_od_par.m_recomp_frac = x[index];
		index++;
	}

	ms_od_par.m_N_mc = x[index];
	index++;

	off_design_core(od_error_code);

	if( od_error_code != 0 )
	{
		error_code = od_error_code;
		return;
	}
		
}

double C_RecompCycle::opt_od_eta(const std::vector<double> &x)
{
	CO2_state co2_props;
	
	int index = 0;

	ms_od_par.m_P_mc_in = x[index];
	index++;

	ms_od_par.m_recomp_frac = 0.0;
	if( ms_des_solved.m_is_rc )
	{
		ms_od_par.m_recomp_frac = x[index];
		index++;
	}

	ms_od_par.m_N_mc = x[index];
	index++;

	// ms_od_par.m_P_mc_in = 3438.0;
	// ms_od_par.m_recomp_frac = 0.0;
	// ms_od_par.m_N_mc = 10244.0;

	// return ms_od_par.m_P_mc_in + ms_od_par.m_recomp_frac + ms_od_par.m_N_mc;

	double T_t_in_upper = ms_phx_od_par.m_T_htf_hot - 0.01;
	bool know_T_in_upper = false;
	
	double T_t_in_lower = ms_phx_od_par.m_T_htf_hot - 50.0;
	bool know_T_in_lower = false;

	double T_t_in_guess = ms_od_par.m_T_t_in;

	double diff_T_t_in = ms_od_par.m_tol*2.0;

	int od_error_code = 0;

	double Q_dot_PHX = 0.0;
	double C_dot_htf = 0.0;
	double T_t_in_calc = 0.0;

	for( int iter_T_t_in = 0; fabs(diff_T_t_in) > ms_od_par.m_tol && iter_T_t_in < 50; iter_T_t_in++ )
	{

		if(iter_T_t_in > 0)	// Guess new T_t_in.... diff_T_t_in = (T_t_in_calc - T_t_in_guess) / T_t_in_guess;
		{
			if(od_error_code != 0)
			{
				T_t_in_lower = T_t_in_guess;
				know_T_in_lower = true;
				T_t_in_guess = 0.5*(T_t_in_lower + T_t_in_guess);
			}
			else
			{
				if( diff_T_t_in > 0.0 )	// T_t_in_guess too small
				{
					T_t_in_lower = T_t_in_guess;
					know_T_in_lower = true;
					if( know_T_in_upper )
						T_t_in_guess = 0.5*(T_t_in_lower + T_t_in_upper);
					else
						T_t_in_guess = T_t_in_upper;
				}
				else
				{
					T_t_in_upper = T_t_in_guess;
					know_T_in_upper = true;
					if( know_T_in_lower )
						T_t_in_guess = 0.5*(T_t_in_lower + T_t_in_upper);
					else
						T_t_in_guess = T_t_in_guess - 10.0;
				}
			}
		}

		if(fabs(T_t_in_upper-T_t_in_lower) < 0.1)
		{
			break;
		}

		ms_od_par.m_T_t_in = T_t_in_guess;

		od_error_code = 0;

		off_design_core(od_error_code);

		if( od_error_code != 0 && iter_T_t_in == 0 )
			return 0.0;
		else if( od_error_code != 0 )
			continue;

		// Get off design values for PHX calcs
		double m_dot_PHX = ms_od_solved.m_m_dot_t;
		double T_PHX_in = ms_od_solved.m_temp[5 - 1];

		//double Q_dot_PHX = ms_od_solved.m_Q_dot;

		// Calculate off-design UA
		double m_dot_ratio = 0.5*(ms_phx_od_par.m_m_dot_htf / ms_phx_od_par.m_m_dot_htf_des + m_dot_PHX / ms_des_solved.m_m_dot_t);
		double UA_PHX_od = ms_phx_od_par.m_UA_PHX_des*pow(m_dot_ratio, 0.8);

		double C_dot_co2 = m_dot_PHX*(ms_od_solved.m_enth[6 - 1] - ms_od_solved.m_enth[5 - 1]) /
			(ms_od_solved.m_temp[6 - 1] - ms_od_solved.m_temp[5 - 1]);	//[kW/K]

		C_dot_htf = ms_phx_od_par.m_cp_htf*ms_phx_od_par.m_m_dot_htf;

		double C_dot_min = min(C_dot_co2, C_dot_htf);
		double C_dot_max = max(C_dot_co2, C_dot_htf);

		double C_R = C_dot_min / C_dot_max;

		double NTU = UA_PHX_od / C_dot_min;

		double eff = 0.0;
		if(C_R < 1.0)
			eff = (1.0 - exp(-NTU*(1.0-C_R)))/(1.0 - C_R*exp(-NTU*(1.0-C_R)));
		else
			eff = NTU / (1.0 + NTU);

		Q_dot_PHX = eff * (C_dot_min*(ms_phx_od_par.m_T_htf_hot - T_PHX_in));

		//m_dot(h_t_in - h_phx_in) = Q_dot_PHX
		double h_t_in = ms_od_solved.m_enth[5 - 1] + Q_dot_PHX / m_dot_PHX;

		CO2_PH(ms_od_solved.m_pres[6-1], h_t_in, &co2_props);

		T_t_in_calc = co2_props.temp;

		double T_htf_cold = ms_phx_od_par.m_T_htf_hot - Q_dot_PHX / C_dot_htf;

		diff_T_t_in = (T_t_in_calc - T_t_in_guess) / T_t_in_guess;
	}

	double T_htf_cold = ms_phx_od_par.m_T_htf_hot - Q_dot_PHX / C_dot_htf;

	double eta_thermal = ms_od_solved.m_eta_thermal;

	double diff_T_cold = max(0.0, fabs(ms_phx_od_par.m_T_htf_cold - T_htf_cold) / T_htf_cold - ms_od_par.m_tol);
	//diff_T_cold = 0.0;	// overwrite for now...

	double over_deltaT = max(0.0, fabs(diff_T_t_in) - ms_od_par.m_tol);

	double over_deltaP = max(0.0, ms_od_solved.m_pres[2-1] - ms_des_par.m_P_high_limit);

	double eta_return = eta_thermal*exp(-diff_T_cold)*exp(-over_deltaP)*exp(-over_deltaT);

	if( diff_T_cold == 0.0 && over_deltaT == 0.0 && over_deltaP == 0.0 )
		m_found_opt = true;

	if( eta_return > m_eta_phx_max )
	{
		m_eta_phx_max = eta_return;
		m_over_deltaP_eta_max = over_deltaP;
		m_UA_diff_eta_max = diff_T_t_in;
	}

	return eta_return;
}

double fmin_callback_opt_eta_1(double x, void *data)
{
	C_RecompCycle *frame = static_cast<C_RecompCycle*>(data);

	return frame->opt_eta(x);
}

double nlopt_callback_opt_des_1(const std::vector<double> &x, std::vector<double> &grad, void *data)
{
	C_RecompCycle *frame = static_cast<C_RecompCycle*>(data);
	if( frame != NULL ) return frame->design_point_eta(x);
}

double nlopt_cb_opt_od(const std::vector<double> &x, std::vector<double> &grad, void *data)
{
	C_RecompCycle *frame = static_cast<C_RecompCycle*>(data);
	if( frame != NULL ) return frame->off_design_point_value(x);
}

double nlopt_cb_eta_at_target(const std::vector<double> &x, std::vector<double> &grad, void *data)
{
	C_RecompCycle *frame = static_cast<C_RecompCycle*>(data);
	if( frame != NULL ) return frame->eta_at_target(x);
}

double nlopt_cb_opt_od_eta(const std::vector<double> &x, std::vector<double> &grad, void *data)
{
	C_RecompCycle *frame = static_cast<C_RecompCycle*>(data);
	if( frame != NULL ) return frame->opt_od_eta(x);
}

double P_pseudocritical_1(double T_K)
{
	return (0.191448*T_K + 45.6661)*T_K - 24213.3;
}