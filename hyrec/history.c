/*************************************************************************************************/
/*                 HYREC: Hydrogen and Helium Recombination Code                                 */
/*         Written by Yacine Ali-Haimoud and Chris Hirata (Caltech)                              */
/*                                                                                               */
/*         history.c: compute reionization history                                               */
/*                                                                                               */
/*         Version: January 2011                                                                 */
/*         Revision history:                                                                     */
/*            - written November 2010                                                            */
/*            - January 2011: changed various switches (notably for post-Saha expansions)        */
/*                             so that they remain valid for arbitrary cosmologies               */
/*************************************************************************************************/ 
 
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "hyrectools.h"
#include "helium.h"
#include "hydrogen.h"
#include "history.h"

/************************************************************************************************* 
Cosmological parameters Input/Output 
*************************************************************************************************/

void rec_get_cosmoparam(FILE *fin, FILE *fout, REC_COSMOPARAMS *param) {

  /* Cosmology */
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter CMB temperature today [Kelvin]: ");
  fscanf(fin, "%lg", &(param->T0));
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter baryon density, omega_bh2: ");
  fscanf(fin, "%lg", &(param->obh2));
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter total matter (CDM+baryons) density, omega_mh2: ");
  fscanf(fin, "%lg", &(param->omh2));
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter curvature, omega_kh2: ");
  fscanf(fin, "%lg", &(param->okh2));
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter dark energy density, omega_deh2: ");
  fscanf(fin, "%lg", &(param->odeh2));
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter dark energy equation of state parameters, w wa: ");
  fscanf(fin, "%lg %lg", &(param->w0), &(param->wa));
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter primordial helium mass fraction, Y: ");
  fscanf(fin, "%lg", &(param->Y));
  if (fout!=NULL && PROMPT==1) fprintf(fout, "Enter effective number of neutrino species, N_nu_eff: ");
  fscanf(fin, "%lg", &(param->Nnueff));

  param->nH0 = 11.223846333047*param->obh2*(1.-param->Y);  /* number density of hudrogen today in m-3 */
  param->fHe = param->Y/(1-param->Y)/3.97153;              /* abundance of helium by number */
  

  /* Redshift range */
  param->zstart = 8000.;
  param->zend = 0.;
  param->dlna = 8.49e-5;
  param->nz = (long) floor(2+log((1.+param->zstart)/(1.+param->zend))/param->dlna);  

  if (fout!=NULL && PROMPT==1) fprintf(fout, "\n");
}

/************************************************************************************* 
Hubble expansion parameter in sec^-1 
*************************************************************************************/

double rec_HubbleConstant(REC_COSMOPARAMS *param, double z) {

   int j;
   double rho, rho_i[5], ainv;
   double ogh2;

   ainv = 1.+z; /* inverse scale factor */

   /* Matter */
   rho_i[0] = param->omh2 *ainv*ainv*ainv;

   /* Curvature */
   rho_i[1] = param->okh2 *ainv*ainv;

   /* Dark energy */  
   rho_i[2] = param->odeh2 * pow(ainv, 3*(1+param->w0)) * exp(3*param->wa* (log(ainv)-1.+1./ainv));

   /* Get radiation density.
    * Coefficient based on 1 AU = 1.49597870691e11 m (JPL SSD) */
   ogh2 = 4.48162687719e-7 * param->T0 * param->T0 * param->T0 * param->T0;
   rho_i[3] = ogh2 *ainv*ainv*ainv*ainv;

   /* Neutrino density -- scaled from photons assuming lower temperature */
   rho_i[4] = 0.227107317660239 * rho_i[3] * param->Nnueff;

   /* Total density, including curvature contribution */
   rho = 0.; for(j=0; j<5; j++) rho += rho_i[j];

   /* Conversion to Hubble rate */
   return( 3.2407792896393e-18 * sqrt(rho) );
}

/***************************************************************************************** 
Matter temperature -- 1st order steady state, from Hirata 2008 
******************************************************************************************/

double rec_Tmss(double xe, double Tr, double H, double fHe, double nH, double z, double energy_rate) {
	
	double eV;
	double k_b;
	
	eV=1.60217653e-19; //in J eV^{-1}
	k_b=1.3806503e-23; //in m^2 kg s^{-2} K

	return(Tr/(1.+H/4.91466895548409e-22/Tr/Tr/Tr/Tr*(1.+xe+fHe)/xe)
	       +1/(4.91466895548409e-22*pow(Tr,4)*xe)*2./(3.*k_b)*(1.+2.*xe)/(3.*nH*1e6)
	       *energy_rate);
	
   /* Coefficient = 8 sigma_T a_r / (3 m_e c) */
}

/****************************************************************************************** 
Matter temperature evolution derivative 
******************************************************************************************/

double rec_dTmdlna(double xe, double Tm, double Tr, double H, double fHe, double nH, double z, double energy_rate) {
	
	double k_b;
	
	k_b=1.3806503e-23; //in m^2 kg s^{-2} K
	
	return(-2.*Tm + 4.91466895548409e-22*Tr*Tr*Tr*Tr*xe/(1.+xe+fHe)*(Tr-Tm)/H
	       +2./(3.*k_b)*(1.+2.*xe)/(3*nH*1e6)*energy_rate/(1.+xe+fHe)/H);

   /* Coefficient = 8 sigma_T a_r / (3 m_e c) */
}

/**********************************************************************************************
Second order integrator using derivative from previous time steps
Evolves xe only, assumes Tm is given by the steady-state solution
***********************************************************************************************/

void rec_get_xe_next1(REC_COSMOPARAMS *param, double z1, double xe_in, double *xe_out,
                      HRATEEFF *rate_table, int func_select, unsigned iz, TWO_PHOTON_PARAMS *twog_params,
		      double **logfminus_hist, double *logfminus_Ly_hist[], 
                      double *z_prev, double *dxedlna_prev, double *z_prev2, double *dxedlna_prev2) {

  double dxedlna, Tr, nH, ainv, H, Tm; 
    
    Tr = param->T0 * (ainv=1.+z1);
    nH = param->nH0 * ainv*ainv*ainv;
    H = rec_HubbleConstant(param, z1); 
    Tm = rec_Tmss(xe_in, Tr, H, param->fHe,nH*1e-6, z1, energy_injection_rate(param,z1)); 
   
    #if (MODEL == PEEBLES)
        dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
                                           rec_HPeebles_dxedlna(xe_in, nH*1e-6, H, Tm*kBoltz, Tr*kBoltz,z1, energy_injection_rate(param,z1));
    #elif (MODEL == RECFAST)
        dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
                                           rec_HRecFast_dxedlna(xe_in, nH*1e-6, H, Tm*kBoltz, Tr*kBoltz,z1, energy_injection_rate(param,z1));
    #elif (MODEL == EMLA2s2p)
        dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
                                           rec_HMLA_dxedlna(xe_in, nH*1e-6, H, Tm*kBoltz, Tr*kBoltz, z1, energy_injection_rate(param,z1), rate_table);
    #else
        dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
	  func_select==FUNC_H2G  ? rec_HMLA_2photon_dxedlna(xe_in, nH*1e-6, H, Tm*kBoltz, Tr*kBoltz, rate_table, twog_params, 
							    param->zstart, param->dlna, logfminus_hist, logfminus_Ly_hist, iz, z1, energy_injection_rate(param,z1))
	  :rec_HMLA_dxedlna(xe_in, nH*1e-6, H, Tm*kBoltz, Tr*kBoltz,z1, energy_injection_rate(param,z1), rate_table);
    #endif                    
                      
    *xe_out = xe_in + param->dlna * (1.25 * dxedlna - 0.25 * (*dxedlna_prev2)); 

    *z_prev2       = *z_prev;
    *dxedlna_prev2 = *dxedlna_prev;
    *z_prev        = z1;
    *dxedlna_prev  = dxedlna;
	
}

/**********************************************************************************************
Second order integrator using derivative from previous time steps
Evolves xe and Tm simultaneously
***********************************************************************************************/

void rec_get_xe_next2(REC_COSMOPARAMS *param, double z1, double xe_in, double Tm_in, double *xe_out, double *Tm_out,
                      HRATEEFF *rate_table, int func_select, unsigned iz, TWO_PHOTON_PARAMS *twog_params,
		      double **logfminus_hist, double *logfminus_Ly_hist[], 
                      double *z_prev, double *dxedlna_prev, double *dTmdlna_prev, 
                      double *z_prev2, double *dxedlna_prev2, double *dTmdlna_prev2) {

    double dxedlna, dTmdlna, Tr, nH, ainv, H;  

    Tr = param->T0 * (ainv=1.+z1);
    nH = param->nH0 * ainv*ainv*ainv;
    H = rec_HubbleConstant(param, z1); 
  
    #if (MODEL == PEEBLES)
         dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
                                            rec_HPeebles_dxedlna(xe_in, nH*1e-6, H, Tm_in*kBoltz, Tr*kBoltz, z1, energy_injection_rate(param,z1));
    #elif (MODEL == RECFAST)
         dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
                                            rec_HRecFast_dxedlna(xe_in, nH*1e-6, H, Tm_in*kBoltz, Tr*kBoltz, z1, energy_injection_rate(param,z1));
    #elif (MODEL == EMLA2s2p)
         dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
                                            rec_HMLA_dxedlna(xe_in, nH*1e-6, H, Tm_in*kBoltz, Tr*kBoltz,z1, energy_injection_rate(param,z1), rate_table);
    #else
         dxedlna = func_select==FUNC_HEI  ? rec_helium_dxedt(xe_in, param->nH0, param->T0, param->fHe, H, z1)/H:
                   func_select==FUNC_H2G  ? rec_HMLA_2photon_dxedlna(xe_in, nH*1e-6, H, Tm_in*kBoltz, Tr*kBoltz, rate_table, twog_params, 
                                                                     param->zstart, param->dlna, logfminus_hist, logfminus_Ly_hist, iz, z1,
																	 energy_injection_rate(param,z1)):        
	           func_select==FUNC_HMLA ? rec_HMLA_dxedlna(xe_in, nH*1e-6, H, Tm_in*kBoltz, Tr*kBoltz,z1, energy_injection_rate(param,z1), rate_table)
                                           :rec_HPeebles_dxedlna(xe_in, nH*1e-6, H, Tm_in*kBoltz, Tr*kBoltz, z1, energy_injection_rate(param,z1)); /* used for z < 20 only */
    #endif    

	 dTmdlna = rec_dTmdlna(xe_in, Tm_in, Tr, H, param->fHe,nH*1e-6,z1, energy_injection_rate(param,z1));
                                          
    *xe_out = xe_in + param->dlna * (1.25 * dxedlna - 0.25 * (*dxedlna_prev2)); 
    *Tm_out = Tm_in + param->dlna * (1.25 * dTmdlna - 0.25 * (*dTmdlna_prev2));

    *z_prev2       = *z_prev;
    *dxedlna_prev2 = *dxedlna_prev;
    *dTmdlna_prev2 = *dTmdlna_prev;
    *z_prev        = z1;
    *dxedlna_prev  = dxedlna;
    *dTmdlna_prev  = dTmdlna;
	
	
}

/**************************************************************************************************** 
Builds a recombination history 
****************************************************************************************************/

void rec_build_history(REC_COSMOPARAMS *param, HRATEEFF *rate_table, TWO_PHOTON_PARAMS *twog_params,
                       double *xe_output, double *Tm_output) {

  
   long iz;
   double **logfminus_hist; 
   double *logfminus_Ly_hist[3];
   double H, z, z_prev, dxedlna_prev, z_prev2, dxedlna_prev2, dTmdlna_prev, dTmdlna_prev2;
   double Delta_xe;
	
   /* history of photon occupation numbers */
   logfminus_hist = create_2D_array(NVIRT, param->nz);
   logfminus_Ly_hist[0] = create_1D_array(param->nz);   /* Ly-alpha */
   logfminus_Ly_hist[1] = create_1D_array(param->nz);   /* Ly-beta  */ 
   logfminus_Ly_hist[2] = create_1D_array(param->nz);   /* Ly-gamma */ 


   z = param->zstart; 


   /********* He II + III Saha phase *********/
   Delta_xe = 1.;   /* Delta_xe = xHeIII */

   for(iz=0; iz<param->nz && Delta_xe > 1e-9; iz++) {
      z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.;
      xe_output[iz] = rec_sahaHeII(param->nH0,param->T0,param->fHe,z, &Delta_xe);
      Tm_output[iz] = param->T0 * (1.+z); 
   }

   /******* He I + II post-Saha phase *********/
   Delta_xe = 0.;     /* Delta_xe = xe - xe(Saha) */
   
   for (; iz<param->nz && Delta_xe < 5e-4; iz++) {    
      z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.; 
      xe_output[iz] = xe_PostSahaHe(param->nH0,param->T0,param->fHe, rec_HubbleConstant(param,z), z, &Delta_xe);
      Tm_output[iz] = param->T0 * (1.+z);
   }
   
   /****** Segment where we follow the helium recombination evolution, Tm fixed to steady state *******/ 

   z_prev2 = (1.+param->zstart)*exp(-param->dlna*(iz-3)) - 1.; 
   dxedlna_prev2 = (xe_output[iz-2] - xe_output[iz-4])/2./param->dlna;  

   z_prev = (1.+param->zstart)*exp(-param->dlna*(iz-2)) - 1.; 
   dxedlna_prev = (xe_output[iz-1] - xe_output[iz-3])/2./param->dlna;
      
   Delta_xe = 1.;  /* Difference between xe and H-Saha value */
    
   for(; iz<param->nz && (Delta_xe > 1e-4 || z > 1650.); iz++) {
   
      rec_get_xe_next1(param, z, xe_output[iz-1], xe_output+iz, rate_table, FUNC_HEI, iz-1, twog_params,
		     logfminus_hist, logfminus_Ly_hist, &z_prev, &dxedlna_prev, &z_prev2, &dxedlna_prev2); 
      
      z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.; 
      Tm_output[iz] = rec_Tmss(xe_output[iz], param->T0*(1.+z), rec_HubbleConstant(param, z), param->fHe,param->nH0*cube(1.+z), z_prev2 , energy_injection_rate(param,z)); 

      /* Starting to populate the photon occupation number with thermal values */
      update_fminus_Saha(logfminus_hist, logfminus_Ly_hist, xe_output[iz], param->T0*(1.+z)*kBoltz, 
                         param->nH0*cube(1.+z)*1e-6, twog_params, param->zstart, param->dlna, iz, z, 0);    
          
      Delta_xe = fabs(xe_output[iz]- rec_saha_xe_H(param->nH0, param->T0, z)); 
    }

 
   /******* Hydrogen post-Saha equilibrium phase *********/
   Delta_xe = 0.;  /*Difference between xe and Saha value */
   
   for(; iz<param->nz && Delta_xe < 5e-5; iz++) {
      z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.; 
      H = rec_HubbleConstant(param,z);
      xe_output[iz] =  xe_PostSahaH(param->nH0*cube(1.+z)*1e-6, H, kBoltz*param->T0*(1.+z), rate_table, twog_params,
				    param->zstart, param->dlna, logfminus_hist, logfminus_Ly_hist, iz, z, &Delta_xe, MODEL, energy_injection_rate(param,z)); 
      Tm_output[iz] = rec_Tmss(xe_output[iz], param->T0*(1.+z), H, param->fHe, param->nH0*cube(1.+z), z_prev2 , energy_injection_rate(param,z));
    }   
  
    /******* Segment where we follow the hydrogen recombination evolution with two-photon processes
             Tm fixed to steady state ******/
    
    z_prev2 = (1.+param->zstart)*exp(-param->dlna*(iz-3)) - 1.; 
    dxedlna_prev2 = (xe_output[iz-2] - xe_output[iz-4])/2./param->dlna;  
  
    z_prev = (1.+param->zstart)*exp(-param->dlna*(iz-2)) - 1.; 
    dxedlna_prev = (xe_output[iz-1] - xe_output[iz-3])/2./param->dlna;
        
    for(; iz<param->nz && 1.-Tm_output[iz-1]/param->T0/(1.+z) < 5e-4 && z > 700.; iz++) {  
    
       rec_get_xe_next1(param, z, xe_output[iz-1], xe_output+iz, rate_table, FUNC_H2G, iz-1, twog_params,
               	      logfminus_hist, logfminus_Ly_hist, &z_prev, &dxedlna_prev, &z_prev2, &dxedlna_prev2); 
       z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.; 
       Tm_output[iz] = rec_Tmss(xe_output[iz], param->T0*(1.+z), rec_HubbleConstant(param, z), param->fHe, param->nH0*cube(1.+z)*1e-6, z_prev2, energy_injection_rate(param,z)); 
    }
     
   /******* Segment where we follow the hydrogen recombination evolution with two-photon processes
            AND Tm evolution ******/

    dTmdlna_prev2 = rec_dTmdlna(xe_output[iz-3], Tm_output[iz-3], param->T0*(1.+z_prev2), 
                                rec_HubbleConstant(param, z_prev2), param->fHe, param->nH0*cube(1.+z), z_prev2, energy_injection_rate(param,z_prev2));
    dTmdlna_prev  = rec_dTmdlna(xe_output[iz-2], Tm_output[iz-2], param->T0*(1.+z_prev), 
                                rec_HubbleConstant(param, z_prev), param->fHe, param->nH0*cube(1.+z), z_prev, energy_injection_rate(param,z_prev));      

    for(; iz<param->nz && z > 700.; iz++) { 
        
        rec_get_xe_next2(param, z, xe_output[iz-1], Tm_output[iz-1], xe_output+iz, Tm_output+iz, rate_table, FUNC_H2G,  
                        iz-1, twog_params, logfminus_hist, logfminus_Ly_hist, &z_prev, &dxedlna_prev, &dTmdlna_prev, 
                        &z_prev2, &dxedlna_prev2, &dTmdlna_prev2); 
        z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.;
     }

    /***** Segment where we follow Tm as well as xe *****/ 
    /* Radiative transfer effects switched off here */
    for(; iz<param->nz && z>20.; iz++) { 
        
        rec_get_xe_next2(param, z, xe_output[iz-1], Tm_output[iz-1], xe_output+iz, Tm_output+iz, rate_table, FUNC_HMLA,  
                        iz-1, twog_params, logfminus_hist, logfminus_Ly_hist, &z_prev, &dxedlna_prev, &dTmdlna_prev, 
                        &z_prev2, &dxedlna_prev2, &dTmdlna_prev2); 
        z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.;
    }
  
    /*** For z < 20 use Peeble's model. The precise model does not metter much here as 
            1) the free electron fraction is basically zero (~1e-4) in any case and 
            2) the universe is going to be reionized around that epoch 
         Tm is still evolved explicitly ***/
    for(; iz<param->nz; iz++) { 
        
        rec_get_xe_next2(param, z, xe_output[iz-1], Tm_output[iz-1], xe_output+iz, Tm_output+iz, rate_table, FUNC_PEEBLES,  
                        iz-1, twog_params, logfminus_hist, logfminus_Ly_hist, &z_prev, &dxedlna_prev, &dTmdlna_prev, 
                        &z_prev2, &dxedlna_prev2, &dTmdlna_prev2); 
        z = (1.+param->zstart)*exp(-param->dlna*iz) - 1.;
    }

    /* Cleanup */
    free_2D_array(logfminus_hist, NVIRT);
    free(logfminus_Ly_hist[0]); 
    free(logfminus_Ly_hist[1]);
    free(logfminus_Ly_hist[2]);

}

double energy_injection_rate(REC_COSMOPARAMS *param, 
			     double z) {

  double p_annihilation_at_z;
  double coeff;
  coeff=1.932e-10; //pow(0.71*1.e5/_Mpc_over_m_,2)*3/8./_PI_/_G_*_c_*_c_*pba->Omega0_cdm;


  /*redshift-dependent annihilation parameter*/
	
  if (z>2500) {
    p_annihilation_at_z=param->p_ann*exp(-param->alpha*0.838490285049671);
  }
  else if(z>30){
    p_annihilation_at_z=param->p_ann*exp(param->alpha*(log((1+z)/2501)*log((1+z)/2501)-0.838490285049671));
    //coeff=log(1001/2501)*log(1001/2501)
  }
  else{
    p_annihilation_at_z=param->p_ann*exp(param->alpha*(log(31./2501)*log(31./2501)-0.838490285049671));
  }

  return pow(param->omh2*4.827652e-18,2)*pow((1.+z),6)*p_annihilation_at_z+coeff*pow((1+z),3)*param->p_dec;  

}