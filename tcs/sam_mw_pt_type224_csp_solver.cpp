#define _TCSTYPEINTERFACE_
#include "tcstype.h"
#include "htf_props.h"
//#include "sam_csp_util.h"
//#include "recore/lib_powerblock.h"
#include "powerblock.h"

#include "csp_solver_pc_Rankine_indirect_224.h"
#include "csp_solver_util.h"

using namespace std;

enum{
	P_P_REF,
	P_ETA_REF,
	P_T_HTF_HOT_REF,
	P_T_HTF_COLD_REF,
	P_DT_CW_REF,
	P_T_AMB_DES,
	P_HTF,
	P_FIELD_FL_PROPS,
	P_Q_SBY_FRAC,
	P_P_BOIL,
	P_CT,
	P_STARTUP_TIME,
	P_STARTUP_FRAC,
	P_TECH_TYPE,
	P_T_APPROACH,
	P_T_ITD_DES,
	P_P_COND_RATIO,
	P_PB_BD_FRAC,
	P_PB_INPUT_FILE,
	P_P_COND_MIN,
	P_N_PL_INC,
	P_F_WC,

	I_MODE,
	I_T_HTF_HOT,
	I_M_DOT_HTF,
	I_T_WB,
	I_DEMAND_VAR,
	I_STANDBY_CONTROL,
	I_T_DB,
	I_P_AMB,
	I_TOU,
	I_RH,

	O_P_CYCLE,
	O_ETA,
	O_T_HTF_COLD,
	O_M_DOT_MAKEUP,
	O_M_DOT_DEMAND,
	O_M_DOT_HTF_OUT,
	O_M_DOT_HTF_REF,
	O_W_COOL_PAR,
	O_P_REF_OUT,
	O_F_BAYS,
	O_P_COND,

	//Include N_max
	N_MAX
};

tcsvarinfo sam_mw_pt_type224_variables[] = {
	{ TCS_PARAM,          TCS_NUMBER,             P_P_REF,                  "P_ref",                                     "Reference output electric power at design condition",           "MW",             "",             "",          "111" },
	{ TCS_PARAM,          TCS_NUMBER,           P_ETA_REF,                "eta_ref",                                     "Reference conversion efficiency at design condition",         "none",             "",             "",       "0.3774" },
	{ TCS_PARAM,          TCS_NUMBER,     P_T_HTF_HOT_REF,          "T_htf_hot_ref",                                               "Reference HTF inlet temperature at design",            "C",             "",             "",          "391" },
	{ TCS_PARAM,          TCS_NUMBER,    P_T_HTF_COLD_REF,         "T_htf_cold_ref",                                              "Reference HTF outlet temperature at design",            "C",             "",             "",          "293" },
	{ TCS_PARAM,          TCS_NUMBER,         P_DT_CW_REF,              "dT_cw_ref",                                   "Reference condenser cooling water inlet/outlet T diff",            "C",             "",             "",           "10" },
	{ TCS_PARAM,          TCS_NUMBER,         P_T_AMB_DES,              "T_amb_des",                                           "Reference ambient temperature at design point",            "C",             "",             "",           "20" },
	{ TCS_PARAM,          TCS_NUMBER,               P_HTF,                    "HTF",                                             "Integer flag identifying HTF in power block",         "none",             "",             "",           "21" },
	{ TCS_PARAM,          TCS_NUMBER,    P_FIELD_FL_PROPS,         "field_fl_props",                                                  "User defined field fluid property data",            "-",             "7 columns (T,Cp,dens,visc,kvisc,cond,h), at least 3 rows",        "",        ""},
	{ TCS_PARAM,          TCS_NUMBER,        P_Q_SBY_FRAC,             "q_sby_frac",                                     "Fraction of thermal power required for standby mode",         "none",             "",             "",          "0.2" },
	{ TCS_PARAM,          TCS_NUMBER,            P_P_BOIL,                 "P_boil",                                                               "Boiler operating pressure",          "bar",             "",             "",          "100" },
	{ TCS_PARAM,          TCS_NUMBER,                P_CT,                     "CT",                                        "Flag for using dry cooling or wet cooling system",         "none",             "",             "",            "1" },
	{ TCS_PARAM,          TCS_NUMBER,      P_STARTUP_TIME,           "startup_time",                                                     "Time needed for power block startup",           "hr",             "",             "",          "0.5" },
	{ TCS_PARAM,          TCS_NUMBER,      P_STARTUP_FRAC,           "startup_frac",                                     "Fraction of design thermal power needed for startup",         "none",             "",             "",          "0.2" },
	{ TCS_PARAM,          TCS_NUMBER,         P_TECH_TYPE,              "tech_type",                       "Flag indicating which coef. set to use. (1=tower,2=trough,3=user)",         "none",             "",             "",            "2" },
	{ TCS_PARAM,          TCS_NUMBER,        P_T_APPROACH,             "T_approach",                                                      "Cooling tower approach temperature",            "C",             "",             "",            "5" },
	{ TCS_PARAM,          TCS_NUMBER,         P_T_ITD_DES,              "T_ITD_des",                                                            "ITD at design for dry system",            "C",             "",             "",           "16" },
	{ TCS_PARAM,          TCS_NUMBER,      P_P_COND_RATIO,           "P_cond_ratio",                                                                "Condenser pressure ratio",         "none",             "",             "",       "1.0028" },
	{ TCS_PARAM,          TCS_NUMBER,        P_PB_BD_FRAC,             "pb_bd_frac",                                                    "Power block blowdown steam fraction ",         "none",             "",             "",         "0.02" },
	{ TCS_PARAM,          TCS_STRING,     P_PB_INPUT_FILE,          "pb_input_file",                                                       "Power block coefficient file name",         "none",             "",             "","pb_coef_file.in" },
	{ TCS_PARAM,          TCS_NUMBER,        P_P_COND_MIN,             "P_cond_min",                                                              "Minimum condenser pressure",         "inHg",             "",             "",         "1.25" },
	{ TCS_PARAM,          TCS_NUMBER,          P_N_PL_INC,               "n_pl_inc",                            "Number of part-load increments for the heat rejection system",         "none",             "",             "",            "2" },
	{ TCS_PARAM,           TCS_ARRAY,              P_F_WC,                   "F_wc",                                   "Fraction indicating wet cooling use for hybrid system",         "none",             "",             "","0,0,0,0,0,0,0,0,0" },

	{ TCS_INPUT,          TCS_NUMBER,              I_MODE,                   "mode",                                          "Cycle part load control, from plant controller",         "none",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,         I_T_HTF_HOT,              "T_htf_hot",                                            "Hot HTF inlet temperature, from storage tank",            "C",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,         I_M_DOT_HTF,              "m_dot_htf",                                                                      "HTF mass flow rate",        "kg/hr",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,              I_T_WB,                   "T_wb",                                                            "Ambient wet bulb temperature",            "C",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,        I_DEMAND_VAR,             "demand_var",                                              "Control signal indicating operational mode",         "none",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,   I_STANDBY_CONTROL,        "standby_control",                                                  "Control signal indicating standby mode",         "none",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,              I_T_DB,                   "T_db",                                                            "Ambient dry bulb temperature",            "C",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,             I_P_AMB,                  "P_amb",                                                                        "Ambient pressure",          "mbar",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,               I_TOU,                    "TOU",                                                              "Current Time-of-use period",         "none",             "",             "",             "" },
	{ TCS_INPUT,          TCS_NUMBER,                I_RH,                     "rh",                                                    "Relative humidity of the ambient air",         "none",             "",             "",             "" },

	{ TCS_OUTPUT,          TCS_NUMBER,           O_P_CYCLE,                "P_cycle",                                                                      "Cycle power output",          "MWe",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,               O_ETA,                    "eta",                                                                "Cycle thermal efficiency",         "none",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,        O_T_HTF_COLD,             "T_htf_cold",                                                 "Heat transfer fluid outlet temperature ",            "C",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,      O_M_DOT_MAKEUP,           "m_dot_makeup",                                                          "Cooling water makeup flow rate",        "kg/hr",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,      O_M_DOT_DEMAND,           "m_dot_demand",                                               "HTF required flow rate to meet power load",        "kg/hr",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,     O_M_DOT_HTF_OUT,          "m_dot_htf_out",                                    "Actual HTF flow rate passing through the power cycle",        "kg/hr",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,     O_M_DOT_HTF_REF,          "m_dot_htf_ref",                                            "Calculated reference HTF flow rate at design",        "kg/hr",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,        O_W_COOL_PAR,             "W_cool_par",                                                           "Cooling system parasitic load",          "MWe",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,         O_P_REF_OUT,              "P_ref_out",                                   "Reference power level output at design (mirror param)",          "MWe",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,            O_F_BAYS,                 "f_bays",                                               "Fraction of operating heat rejection bays",         "none",             "",             "",             "" },
	{ TCS_OUTPUT,          TCS_NUMBER,            O_P_COND,                 "P_cond",                                                                      "Condenser pressure",           "Pa",             "",             "",             "" },
	
	{ TCS_INVALID,    TCS_INVALID,    N_MAX,                0,                    0,                                                        0,                0,        0,        0 }
};

class sam_mw_pt_type224 : public tcstypeinterface
{
private:
	C_pc_Rankine_indirect_224 mc_power_cycle;
	C_csp_weatherreader::S_outputs ms_weather;
	C_csp_solver_sim_info ms_sim_info;
	C_pc_Rankine_indirect_224::S_inputs ms_inputs;
	C_pc_Rankine_indirect_224::S_outputs ms_outputs;

public:

	sam_mw_pt_type224(tcscontext *cxt, tcstypeinfo *ti)
		: tcstypeinterface(cxt, ti)
	{
	}

	virtual ~sam_mw_pt_type224(){
	}

	virtual int init()
	{
		C_pc_Rankine_indirect_224::S_params * p_params = &mc_power_cycle.ms_params;

		//Get values for any parameters here
		p_params->m_P_ref = value(P_P_REF);						//Reference output electric power at design condition [MW]
		p_params->m_eta_ref = value(P_ETA_REF);					//Reference conversion efficiency at design condition [none]
		p_params->m_T_htf_hot_ref = value(P_T_HTF_HOT_REF);		//Reference HTF inlet temperature at design [C]
		p_params->m_T_htf_cold_ref = value(P_T_HTF_COLD_REF);	//Reference HTF outlet temperature at design [C]
		p_params->m_dT_cw_ref = value(P_DT_CW_REF);				//Reference condenser cooling water inlet/outlet T diff [C]
		p_params->m_T_amb_des = value(P_T_AMB_DES);				//Reference ambient temperature at design point [C]
		p_params->m_pc_fl = (int)value(P_HTF);					//Integer flag identifying HTF in power block [none]
		
		int n_rows = -1;
		int n_cols = -1;
		double *pc_fl_props = value(P_FIELD_FL_PROPS, &n_rows, &n_cols);
		p_params->m_pc_fl_props.resize(n_rows, n_cols);
		for( int r = 0; r < n_rows; r++ )
			for( int c = 0; c < n_cols; c++ )
				p_params->m_pc_fl_props(r,c) = TCS_MATRIX_INDEX(var(P_FIELD_FL_PROPS),r,c);
		
		
		p_params->m_q_sby_frac = value(P_Q_SBY_FRAC);			//Fraction of thermal power required for standby mode [none]
		p_params->m_P_boil = value(P_P_BOIL);					//Boiler operating pressure [bar]
		p_params->m_CT = (int) value(P_CT);						//Flag for using dry cooling or wet cooling system                [none]
		p_params->m_startup_time = value(P_STARTUP_TIME);		//Time needed for power block startup [hr]
		p_params->m_startup_frac = value(P_STARTUP_FRAC);		//Fraction of design thermal power needed for startup [none]
		p_params->m_tech_type = (int) value(P_TECH_TYPE);		//Flag indicating which coef. set to use. (1=tower,2=trough,3=user) [none]
		p_params->m_T_approach = value(P_T_APPROACH);			//Cooling tower approach temperature [C]
		p_params->m_T_ITD_des = value(P_T_ITD_DES);				//ITD at design for dry system [C]
		p_params->m_P_cond_ratio = value(P_P_COND_RATIO);		//Condenser pressure ratio [none]
		p_params->m_pb_bd_frac = value(P_PB_BD_FRAC);			//Power block blowdown steam fraction  [none]
		p_params->m_P_cond_min = value(P_P_COND_MIN);			//Minimum condenser pressure [inHg]
		p_params->m_n_pl_inc = (int) value(P_N_PL_INC);			//Number of part-load increments for the heat rejection system [none]
		
		int nval_F_wc = -1;
		double *F_wc = value(P_F_WC, &nval_F_wc);		//Fraction indicating wet cooling use for hybrid system [none]
		p_params->m_F_wc.resize(nval_F_wc);
		for( int i = 0; i < nval_F_wc; i++ )
			p_params->m_F_wc[i] = F_wc[i];

		int out_type = -1;
		std::string out_msg = "";

		try
		{
			mc_power_cycle.init();
		}
		catch(C_csp_exception &csp_exception)
		{
			// Report warning before exiting with error
			while( mc_power_cycle.mc_csp_messages.get_message(&out_type, &out_msg) )
			{
				if( out_type == C_csp_messages::NOTICE )
					message(TCS_NOTICE, out_msg.c_str());
				else if( out_type == C_csp_messages::WARNING )
					message(TCS_WARNING, out_msg.c_str());
			}

			message(TCS_ERROR, csp_exception.m_error_message.c_str());
			return -1;
		}

		// If no exception, then report messages and move on
		while( mc_power_cycle.mc_csp_messages.get_message(&out_type, &out_msg) )
		{
			if( out_type == C_csp_messages::NOTICE )
				message(TCS_NOTICE, out_msg.c_str());
			else if( out_type == C_csp_messages::WARNING )
				message(TCS_WARNING, out_msg.c_str());
		}

		return 0;
	}

	virtual int call(double time, double step, int ncall)
	{

		ms_inputs.m_T_htf_hot = value(I_T_HTF_HOT);		//Hot HTF inlet temperature, from storage tank [C]
		ms_inputs.m_m_dot_htf = value(I_M_DOT_HTF);		//HTF mass flow rate [kg/hr]
		ms_weather.m_twet = value(I_T_WB);				//Ambient wet bulb temperature [C]
		ms_inputs.m_standby_control = (int)value(I_STANDBY_CONTROL);		//Control signal indicating standby mode [none]
		ms_weather.m_tdry = value(I_T_DB);				//Ambient dry bulb temperature [C]
		ms_weather.m_pres = value(I_P_AMB);				//Ambient pressure [mbar]
		ms_inputs.m_tou = (int)value(I_TOU);		//Current Time-of-use period [none]
		ms_weather.m_rhum = value(I_RH) / 100.0;			//Relative humidity of the ambient air [none]

		// set sim info
		ms_sim_info.m_time = time;
		ms_sim_info.m_step = step;
		ms_sim_info.m_ncall = ncall;

		int out_type = -1;
		std::string out_msg = "";

		try
		{
			mc_power_cycle.call(&ms_weather, ms_inputs, ms_outputs, &ms_sim_info);
		}
		catch(C_csp_exception &csp_exception)
		{
			// Report warning before exiting with error
			while( mc_power_cycle.mc_csp_messages.get_message(&out_type, &out_msg) )
			{
				if( out_type == C_csp_messages::NOTICE )
					message(TCS_NOTICE, out_msg.c_str());
				else if( out_type == C_csp_messages::WARNING )
					message(TCS_WARNING, out_msg.c_str());
			}

			message(TCS_ERROR, csp_exception.m_error_message.c_str());
			return -1;
		}
		
		// If no exception, then report messages and move on
		while( mc_power_cycle.mc_csp_messages.get_message(&out_type, &out_msg) )
		{
			if( out_type == C_csp_messages::NOTICE )
				message(TCS_NOTICE, out_msg.c_str());
			else if( out_type == C_csp_messages::WARNING )
				message(TCS_WARNING, out_msg.c_str());
		}


		value(O_P_CYCLE, ms_outputs.m_P_cycle);				//[MWe] Cycle power output
		value(O_ETA, ms_outputs.m_eta);						//[none] Cycle thermal efficiency
		value(O_T_HTF_COLD, ms_outputs.m_T_htf_cold);		//[C] Heat transfer fluid outlet temperature 
		value(O_M_DOT_MAKEUP, ms_outputs.m_m_dot_makeup);	//[kg/hr] Cooling water makeup flow rate
		value(O_M_DOT_DEMAND, ms_outputs.m_m_dot_demand);	//[kg/hr] HTF required flow rate to meet power load
		value(O_M_DOT_HTF_OUT, ms_outputs.m_m_dot_htf);		//[kg/hr] Actual HTF flow rate passing through the power cycle
		value(O_M_DOT_HTF_REF, ms_outputs.m_m_dot_htf_ref);	//[kg/hr] Calculated reference HTF flow rate at design
		value(O_W_COOL_PAR, ms_outputs.m_W_cool_par);		//[MWe] Cooling system parasitic load
		value(O_P_REF_OUT, ms_outputs.m_P_ref);				//[MWe] Reference power level output at design (mirror param)
		value(O_F_BAYS, ms_outputs.m_f_hrsys);				//[none] Fraction of operating heat rejection bays
		value(O_P_COND, ms_outputs.m_P_cond);				//[Pa] Condenser pressure

		return 0;



		//mode = (int)value(I_MODE);		//Cycle part load control, from plant controller [none]
		//T_htf_hot = value(I_T_HTF_HOT);		//Hot HTF inlet temperature, from storage tank [C]
		//m_dot_htf = value(I_M_DOT_HTF);		//HTF mass flow rate [kg/hr]
		//T_wb = value(I_T_WB);		//Ambient wet bulb temperature [C]
		//demand_var = value(I_DEMAND_VAR);		//Control signal indicating operational mode [none]
		//standby_control = (int)value(I_STANDBY_CONTROL);		//Control signal indicating standby mode [none]
		//T_db = value(I_T_DB);		//Ambient dry bulb temperature [C]
		//P_amb = value(I_P_AMB);		//Ambient pressure [mbar]
		//TOU = (int)value(I_TOU) - 1;		//Current Time-of-use period [none]
		//rh = value(I_RH)/100.0;		//Relative humidity of the ambient air [none]

		//
		////Set the inputs
		//inputs.mode = mode;
		//inputs.T_htf_hot = T_htf_hot;
		//inputs.m_dot_htf = m_dot_htf;
		//inputs.T_wb = T_wb + 273.15;		//Convert C to K
		//inputs.demand_var = demand_var;
		//inputs.standby_control = standby_control;
		//inputs.T_db = T_db + 273.15;		//Convert C to K
		//inputs.P_amb = P_amb*100.0;			//Convert mbar to Pa
		//inputs.TOU = TOU;
		//inputs.rel_humidity = rh;

		////Get the stored values
		///*
		//stored.iLastStandbyControl = standby_control0;
		//stored.dStartupTimeRemaining = startup_remain0;
		//stored.dLastP_Cycle = P_cycle0;
		//stored.dStartupEnergyRemaining = startup_e_remain0;
		//*/

		////Simulate
		//type224.Execute((long)time, inputs);
		//outputs = type224.GetOutputs();

		//
		////Set the outputs
		//P_cycle = outputs.P_cycle;
		//eta = outputs.eta;
		//T_htf_cold = outputs.T_htf_cold;
		//m_dot_makeup = outputs.m_dot_makeup;
		//m_dot_demand = outputs.m_dot_demand;
		//m_dot_htf_out = outputs.m_dot_htf;
		//m_dot_htf_ref = outputs.m_dot_htf_ref;
		//W_cool_par = outputs.W_cool_par;
		//P_ref_out = outputs.P_ref;
		//f_bays = outputs.f_hrsys;
		//P_cond = outputs.P_cond;
		

		//value(O_P_CYCLE, P_cycle);		//[MWe] Cycle power output
		//value(O_ETA, eta);		//[none] Cycle thermal efficiency
		//value(O_T_HTF_COLD, T_htf_cold);		//[C] Heat transfer fluid outlet temperature 
		//value(O_M_DOT_MAKEUP, m_dot_makeup);		//[kg/hr] Cooling water makeup flow rate
		//value(O_M_DOT_DEMAND, m_dot_demand);		//[kg/hr] HTF required flow rate to meet power load
		//value(O_M_DOT_HTF_OUT, m_dot_htf_out);		//[kg/hr] Actual HTF flow rate passing through the power cycle
		//value(O_M_DOT_HTF_REF, m_dot_htf_ref);		//[kg/hr] Calculated reference HTF flow rate at design
		//value(O_W_COOL_PAR, W_cool_par);		//[MWe] Cooling system parasitic load
		//value(O_P_REF_OUT, P_ref_out);		//[MWe] Reference power level output at design (mirror param)
		//value(O_F_BAYS, f_bays);		//[none] Fraction of operating heat rejection bays
		//value(O_P_COND, P_cond);		//[Pa] Condenser pressure
		
		return 0;
	}

	virtual int converged(double time)
	{

		int out_type = -1;
		std::string out_msg = "";

		try
		{
			mc_power_cycle.converged();
		}
		catch( C_csp_exception &csp_exception )
		{
			// Report warning before exiting with error
			while( mc_power_cycle.mc_csp_messages.get_message(&out_type, &out_msg) )
			{
				if( out_type == C_csp_messages::NOTICE )
					message(TCS_NOTICE, out_msg.c_str());
				else if( out_type == C_csp_messages::WARNING )
					message(TCS_WARNING, out_msg.c_str());
			}

			message(TCS_ERROR, csp_exception.m_error_message.c_str());
			return -1;
		}

		// If no exception, then report messages and move on
		while( mc_power_cycle.mc_csp_messages.get_message(&out_type, &out_msg) )
		{
			if( out_type == C_csp_messages::NOTICE )
				message(TCS_NOTICE, out_msg.c_str());
			else if( out_type == C_csp_messages::WARNING )
				message(TCS_WARNING, out_msg.c_str());
		}

		return 0;






		//Set the stored values for the next time step
		/*
		standby_control0 = stored.iLastStandbyControl;
		startup_remain0 = max(stored.dStartupTimeRemaining-dt/3600., 0.);
		P_cycle0 = stored.dLastP_Cycle;
		startup_e_remain0 = stored.dStartupEnergyRemaining;
		*/

		//return 0;
	}
	
	
};


TCS_IMPLEMENT_TYPE( sam_mw_pt_type224, "Indirect HTF power cycle model", "Mike Wagner", 1, sam_mw_pt_type224_variables, NULL, 1 );