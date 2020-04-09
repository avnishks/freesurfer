/**
 * @brief Fixes the presurf cortex and maps aparc labels to a
 * volume-based segmentation including aparc+aseg and wmparc.
 * this is a replacement for mri_aparc2aseg.
 *
 */
/*
 * Original Author: Douglas N. Greve
 *
 * Copyright © 2011 The General Hospital Corporation (Boston, MA) "MGH"
 *
 * Terms and conditions for use, reproduction, distribution and contribution
 * are found in the 'FreeSurfer Software License Agreement' contained
 * in the file 'LICENSE' found in the FreeSurfer distribution, and here:
 *
 * https://surfer.nmr.mgh.harvard.edu/fswiki/FreeSurferSoftwareLicense
 *
 * Reporting: freesurfer@nmr.mgh.harvard.edu
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "macros.h"
#include "mrisurf.h"
#include "mrisutils.h"
#include "error.h"
#include "diag.h"
#include "mri.h"
#include "mri2.h"
#include "fio.h"
#include "annotation.h"
#include "version.h"
#include "mrisegment.h"
#include "cma.h"
#include "gca.h"
#include "cmdargs.h"
#ifdef _OPENMP
#include "romp_support.h"
#endif
using namespace std;
#undef private

class Surf2VolSeg {
public:
  MRI *outvolseg=NULL;
  MRI *involseg=NULL;
  MRI *ribbon=NULL;
  int DoLH=1, DoRH=1, FixPresurf=0, LabelCortex=0, LabelWM=0;
  std::string involsegpath, ribbonpath, lhwhitepath, 
    rhwhitepath, lhpialpath, rhpialpath;
  std::string lhcortexlabelpath, rhcortexlabelpath;
  std::string lhannotname="lh.aparc.annot", lhannotpath, rhannotname="rh.aparc.annot", rhannotpath;
  int lhbaseoffset = 1000, rhbaseoffset = 2000;
  int nHopsMax = 5;
  int RipUnknown = 0;
  int LabelHypoAsWM = 0;
  int debug = 0;
  double wmparc_dist_thresh = 5.0;
  MATRIX *Vox2RAS=NULL;
  MRIS *lhwhite=NULL, *lhpial=NULL, *rhwhite=NULL, *rhpial=NULL;
  float hashres = 16;
  MHT *lhwhite_hash=NULL, *lhpial_hash=NULL;
  MHT *rhwhite_hash=NULL, *rhpial_hash=NULL;
  Surf2VolSeg(void){};
  Surf2VolSeg(char *subject, char *SD){
    printf("Starting Surf2VolSeg constructor\n");
    SetSubjectPaths(subject, SD);
    LoadData();
    printf("constructor done\n");
    fflush(stdout);
  }
  int LoadData(void);
  void Free(void);
  MRI *RelabelSegVol(void);
  int RelabelSegVox(int c, int r, int s, int *hemi, int *surftype, double *dmin);
  int RelabelSegVoxWithRibbon(int c, int r, int s);
  int RelabelSegVoxWithSurf(MRI *volseg, int c, int r, int s, int LabelType, int *hemi, int *surftype, double *dmin);

  int CRS2SurfRAS(int c, int r, int s, double *x, double *y, double *z);
  MRIS *GetClosestVertexNo(int c, int r, int s, 
			   int AllowLH, int AllowRH, int AllowWhite, int AllowPial,
			   int WhiteDotDir, int PialDotDir,
			   int *vtxno, int *surftype, int *hemi, double *dmin);
  int GetNearestVertexWithAnnot(int CenterVtx, MRI_SURFACE *Surf, int nHops);
  int SetSubjectPaths(char *subject, char *SD);
};

static int  parse_commandline(int argc, char **argv);
static void check_options(void);
static void print_usage(void) ;
static void usage_exit(void);
static void print_help(void) ;
static void print_version(void) ;
static void dump_options(FILE *fp);

int main(int argc, char *argv[]) ;

static char vcid[] = "$Id$";
const char *Progname = NULL;
char *OutSegFile = NULL;
int nthreads=1;
char *SUBJECTS_DIR=NULL;
int debug=0;
Surf2VolSeg s2vseg;
int crsTest=0, ctest=0, rtest=0, stest=0;

/*--------------------------------------------------*/
int main(int argc, char **argv)
{
  int nargs; //, err;

  nargs = handleVersionOption(argc, argv, "mri_surf2volseg");
  if (nargs && argc - nargs == 1)    exit (0);
  argc -= nargs;

  Progname = argv[0] ;
  argc --;
  argv++;
  ErrorInit(NULL, NULL, NULL) ;
  DiagInit(NULL, NULL, NULL) ;

  if(argc == 0) usage_exit();

  SUBJECTS_DIR = getenv("SUBJECTS_DIR");
  if (SUBJECTS_DIR==NULL){
    printf("ERROR: SUBJECTS_DIR not defined in environment\n");
    exit(1);
  }

  parse_commandline(argc, argv);
  check_options();
  dump_options(stdout);

#ifdef _OPENMP
  printf("%d avail.processors, using %d\n",omp_get_num_procs(),omp_get_max_threads());
#endif

  s2vseg.LoadData();

  if(crsTest){
    printf("Running crs test with %d %d %d %d\n",ctest, rtest, stest,s2vseg.debug);
    int hemi,surftype;
    double dmin;
    s2vseg.RelabelSegVox(ctest, rtest, stest,&hemi,&surftype,&dmin);
    printf("Done test\n");
    return(0);
  }

  s2vseg.RelabelSegVol();
  MRIwrite(s2vseg.outvolseg,OutSegFile);
  s2vseg.Free();


  printf("#VMPC# mri_surf2volseg VmPeak  %d\n",GetVmPeak());
  printf("mri_surf2volseg done\n");

  return(0);
  exit(0);
}
/*-----------------------------------------------------------------*/
/*-----------------------------------------------------------------*/
/*-----------------------------------------------------------------*/

/* --------------------------------------------- */
static int parse_commandline(int argc, char **argv)
{
  int  nargc , nargsused;
  char **pargv, *option ;

  if (argc < 1)
  {
    usage_exit();
  }

  nargc   = argc;
  pargv = argv;
  while (nargc > 0)
  {
    option = pargv[0];
    if (debug)
    {
      printf("%d %s\n",nargc,option);
    }
    nargc -= 1;
    pargv += 1;

    nargsused = 0;

    if (!strcasecmp(option, "--help")||
        !strcasecmp(option, "-h")||
        !strcasecmp(option, "--usage")||
        !strcasecmp(option, "-u")) print_help() ;
    else if (!strcasecmp(option, "--version")) print_version() ;
    else if (!strcasecmp(option, "--debug")) {
      debug = 1;
      s2vseg.debug = 1;
    }
    else if (!strcasecmp(option, "--lh")) {
      s2vseg.DoLH = 1;
      s2vseg.DoRH = 0;
    }
    else if (!strcasecmp(option, "--rh")) {
      s2vseg.DoLH = 0;
      s2vseg.DoRH = 1;
    }
    else if (!strcasecmp(option, "--hypo-as-wm"))  s2vseg.LabelHypoAsWM = 1;
    else if (!strcasecmp(option, "--rip-unknown")) s2vseg.RipUnknown = 1;
    else if (!strcmp(option, "--i")){
      if(nargc < 1) CMDargNErr(option,1);
      s2vseg.involsegpath = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--fix-presurf-with-ribbon")){
      if(nargc < 1) CMDargNErr(option,1);
      s2vseg.ribbonpath   = pargv[0];
      s2vseg.FixPresurf   = 1;
      s2vseg.RipUnknown = 0;
      nargsused = 1;
    }
    else if (!strcmp(option, "--label-cortex")){
      s2vseg.LabelCortex = 1;
      s2vseg.RipUnknown = 1;
    }
    else if (!strcmp(option, "--label-wm")){
      s2vseg.LabelWM = 1;
      s2vseg.RipUnknown = 1;
    }
    else if (!strcmp(option, "--lh-white")){
      if (nargc < 1) CMDargNErr(option,1);
      s2vseg.lhwhitepath = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--lh-pial")){
      if (nargc < 1) CMDargNErr(option,1);
      s2vseg.lhpialpath = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--rh-white")){
      if (nargc < 1) CMDargNErr(option,1);
      s2vseg.rhwhitepath = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--rh-pial")){
      if (nargc < 1) CMDargNErr(option,1);
      s2vseg.rhpialpath = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--lh-cortex-mask")){
      if(nargc < 1) CMDargNErr(option,1);
      s2vseg.lhcortexlabelpath = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--rh-cortex-mask")){
      if(nargc < 1) CMDargNErr(option,1);
      s2vseg.rhcortexlabelpath = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--lh-annot")){
      if(nargc < 2) CMDargNErr(option,2);
      s2vseg.lhannotpath = pargv[0];
      sscanf(pargv[1],"%d",&(s2vseg.lhbaseoffset));
      nargsused = 2;
    }
    else if (!strcmp(option, "--rh-annot")){
      if(nargc < 2) CMDargNErr(option,2);
      s2vseg.rhannotpath = pargv[0];
      sscanf(pargv[1],"%d",&(s2vseg.rhbaseoffset));
      nargsused = 2;
    }
    else if (!strcmp(option, "--o")){
      if (nargc < 1) CMDargNErr(option,1);
      OutSegFile = pargv[0];
      nargsused = 1;
    }
    else if (!strcmp(option, "--sd"))    {
      if (nargc < 1)  CMDargNErr(option,1);
      SUBJECTS_DIR = pargv[0];
      setenv("SUBJECTS_DIR",SUBJECTS_DIR,1);
      nargsused = 1;
    }
    else if (!strcmp(option, "--hashres")) {
      if (nargc < 1)CMDargNErr(option,1);
      sscanf(pargv[0],"%f",&s2vseg.hashres);
      nargsused = 1;
    }
    else if (!strcmp(option, "--wmparc-dmax"))
    {
      if (nargc < 1)CMDargNErr(option,1);
      sscanf(pargv[0],"%lf",&s2vseg.wmparc_dist_thresh);
      nargsused = 1;
    }
    else if (!strcmp(option, "--nhops")){
      if (nargc < 1)CMDargNErr(option,1);
      sscanf(pargv[0],"%d",&s2vseg.nHopsMax);
      nargsused = 1;
    }
    else if (!strcmp(option, "--crs-test"))
    {
      if (nargc < 3) CMDargNErr(option,3);
      sscanf(pargv[0],"%d",&ctest);
      sscanf(pargv[1],"%d",&rtest);
      sscanf(pargv[2],"%d",&stest);
      crsTest = 1;
      s2vseg.debug = 1;
      debug = 1;
      nargsused = 3;
    }
    else if(!strcasecmp(option, "--threads") || !strcasecmp(option, "--nthreads") ){
      if(nargc < 1) CMDargNErr(option,1);
      sscanf(pargv[0],"%d",&nthreads);
      #ifdef _OPENMP
      omp_set_num_threads(nthreads);
      #endif
      nargsused = 1;
    } 
    else{
      printf("ERROR: Option %s unknown\n",option);
      if (CMDsingleDash(option)) printf("       Did you really mean -%s ?\n",option);
      exit(-1);
    }
    nargc -= nargsused;
    pargv += nargsused;
  }
  return(0);
}
/* ------------------------------------------------------ */
static void usage_exit(void)
{
  print_usage() ;
  exit(1) ;
}
/* --------------------------------------------- */
#include "mri_surf2volseg.help.xml.h"
static void print_usage(void)
{
  outputHelpXml(mri_surf2volseg_help_xml,mri_surf2volseg_help_xml_len);
}
/* --------------------------------------------- */
static void print_help(void)
{
  outputHelpXml(mri_surf2volseg_help_xml,mri_surf2volseg_help_xml_len);
  exit(1) ;
}
/* --------------------------------------------- */
static void print_version(void)
{
  printf("%s\n", vcid) ;
  exit(1) ;
}
/* --------------------------------------------- */
static void check_options(void)
{
  if(OutSegFile == NULL){
    printf("ERROR: must specify an output with --o\n");
    exit(1);
  }
  if(s2vseg.involsegpath.length() == 0){
    printf("ERROR: must specify an input segmentation with --i\n");
    exit(1);
  }

  if(s2vseg.DoLH && (s2vseg.lhwhitepath.length() == 0 || s2vseg.lhpialpath.length() == 0)){
    printf("ERROR: you must include lh white and pial surfaces\n");
    exit(1);
  }

  if(s2vseg.DoRH && (s2vseg.rhwhitepath.length() == 0 || s2vseg.rhpialpath.length() == 0)){
    printf("ERROR: you must include rh white and pial surfaces\n");
    exit(1);
  }

  if(s2vseg.DoLH && s2vseg.lhcortexlabelpath.length() == 0){
    printf("ERROR: you must include an lh cortex mask \n");
    exit(1);
  }
  if(s2vseg.DoRH && s2vseg.rhcortexlabelpath.length() == 0){
    printf("ERROR: you must include an rh cortex mask \n");
    exit(1);
  }

  if(s2vseg.FixPresurf + s2vseg.LabelCortex + s2vseg.LabelWM == 0){
    printf("ERROR: must select one of --fix-presurf-with-ribbon, --label-cortex, or --label-wm\n");
    exit(1);
  }
  if(s2vseg.FixPresurf + s2vseg.LabelCortex + s2vseg.LabelWM != 1){
    printf("ERROR: can only do one of --fix-presurf-with-ribbon, --label-cortex, or --label-wm\n");
    exit(1);
  }

  if(s2vseg.FixPresurf){
    if(s2vseg.lhannotpath.length() || s2vseg.rhannotpath.length()){
      printf("ERROR: do not include annotations when fixing presurf with ribbon\n");
      exit(1);
    }
    if(s2vseg.RipUnknown){
      printf("ERROR: do not use --rip-unknown when fixing presurf with ribbon\n");
      exit(1);
    }
  }

  if(s2vseg.LabelCortex || s2vseg.LabelWM){
    if((s2vseg.DoLH && s2vseg.lhannotpath.length()==0) || (s2vseg.DoRH && s2vseg.rhannotpath.length()==0) ){
      printf("ERROR: you must include annotations when labeling cortex or WM\n");
      exit(1);
    }
  }

  //if (!fio_FileExistsReadable(CtxSegFile))
  //FixParaHipWM  = 0;

  return;
}

/* --------------------------------------------- */
static void dump_options(FILE *fp)
{
  fprintf(fp,"SUBJECTS_DIR %s\n",SUBJECTS_DIR);
  fprintf(fp,"outvol %s\n",OutSegFile);
  return;
}


int Surf2VolSeg::RelabelSegVox(int c, int r, int s, int *hemi, int *surftype, double *dmin)
{
  int volsegid,newsegid;
  volsegid = MRIgetVoxVal(involseg,c,r,s,0);
  newsegid = volsegid;
  if(debug) printf("RelabelSegVox(): %d %d %d   %d %d %d   %d\n",c,r,s,FixPresurf,LabelCortex,LabelWM,volsegid);

  if(FixPresurf){
    newsegid = Surf2VolSeg::RelabelSegVoxWithRibbon(c,r,s);
  }
  if(LabelCortex && IS_CORTEX(volsegid)){
    newsegid = Surf2VolSeg::RelabelSegVoxWithSurf(involseg,c, r, s, 1, hemi, surftype, dmin);
    if(newsegid != 0){
      if(*hemi == 1) newsegid += lhbaseoffset;
      if(*hemi == 2) newsegid += rhbaseoffset;
    }
  }
  if(LabelWM && (IS_WHITE_CLASS(volsegid) || IS_HYPO(volsegid))){
    newsegid = Surf2VolSeg::RelabelSegVoxWithSurf(involseg,c, r, s, 2, hemi, surftype, dmin);
    if(*dmin > wmparc_dist_thresh) newsegid = 0;
    if(*hemi == 1) {
      if(newsegid > 0) newsegid += lhbaseoffset;
      else             newsegid = 5001;
    }
    if(*hemi == 2){
      if(newsegid > 0) newsegid += rhbaseoffset;
      else             newsegid = 5002;
    }
  }
  if(debug) printf("RelabelSegVox(): %d %d %d   %d %d   %d %d %g\n",c,r,s,volsegid,newsegid,*hemi,*surftype,*dmin);

  return(newsegid);
}

MRI *Surf2VolSeg::RelabelSegVol(void)
{
  int c,nrelabled=0;

  MHT_maybeParallel_begin();
  #ifdef HAVE_OPENMP
  #pragma omp parallel for reduction(+ : nrelabled)
  #endif
  for (c=0; c < involseg->width; c++){
    printf("%3d ",c);
    if (c%20 ==19) printf("\n");
    fflush(stdout);
    int r,s,volsegid,newsegid,hemi,surftype;
    double dmin;
    for (r=0; r < involseg->height; r++)    {
      for (s=0; s < involseg->depth; s++)      {
        volsegid = MRIgetVoxVal(involseg,c,r,s,0);
	newsegid = RelabelSegVox(c, r, s, &hemi, &surftype, &dmin);
	MRIsetVoxVal(outvolseg,c,r,s,0,newsegid);
	if(volsegid != newsegid) nrelabled++;
      }
    }
  }
  MHT_maybeParallel_end();
  printf("\n");
  printf("nrelabeled = %d\n",nrelabled);

  return(outvolseg);
}

int Surf2VolSeg::RelabelSegVoxWithRibbon(int c, int r, int s)
{
  int volsegid,RibbonLabel;

  volsegid = MRIgetVoxVal(involseg,c,r,s,0);
  RibbonLabel = MRIgetVoxVal(ribbon,c,r,s,0);
  if(debug) printf("RelabelSegVoxWithRibbon(): %d %d %d   %d %d \n",c,r,s,volsegid,RibbonLabel);

  // If the input seg label is not cortex or cerebral WM or cblum
  // cortex or WM hypo or unknown, don't bother relabel it
  if(!IS_CORTEX(volsegid) && !IS_WHITE_CLASS(volsegid) && 
     !IS_CEREBELLAR_GM(volsegid) && !IS_HYPO(volsegid) && 
     volsegid != Unknown)
    return(volsegid);
  
  // Both are unknown. This will handle most of the background voxels
  if(RibbonLabel == Unknown && volsegid == Unknown) return(volsegid);

  if(IS_CORTEX(volsegid)){
    if(RibbonLabel == Unknown) return(RibbonLabel); //relabel
    if(IS_CORTEX(RibbonLabel)) return(volsegid); // not relabeled
    if(IS_WHITE_CLASS(RibbonLabel)) return(RibbonLabel); //relabel
  }

  if(IS_CEREBELLAR_GM(volsegid)){
    if(RibbonLabel == Unknown) return(volsegid);  // not relabeled
    if(IS_CORTEX(RibbonLabel)) return(RibbonLabel);//relabel
    if(IS_WHITE_CLASS(RibbonLabel)) return(RibbonLabel); //relabled, rare    
  }

  if(IS_HYPO(volsegid)){
    if(RibbonLabel == Unknown) return(RibbonLabel); //relabeled, rare
    if(IS_CORTEX(RibbonLabel)) return(RibbonLabel); //relabel
    if(IS_WHITE_CLASS(RibbonLabel)){
      if(LabelHypoAsWM == 0) return(volsegid);    // not relabeled
      else                   return(RibbonLabel); //relabel
    }
  }

  if(IS_WHITE_CLASS(volsegid)){
    if(IS_CORTEX(RibbonLabel)) return(RibbonLabel); //relabel
    if(IS_WHITE_CLASS(RibbonLabel)) return(volsegid);
  }

  // Can only get here if volsegid is WM and Ribbon is unknown
  // We are outside of the ribbon (unknown) and the aseg label is cerebral WM. 
  // Cases:
  // - A gyrus is badly mislabeled in the aseg such that sulcal CSF is
  //   labeled as WM. This definitely needs to be relabeled as something
  //   not WM
  // - Near the medial wall (eg, between the surface and hippo/amyg, Martin R's
  //   case). Use the aseg here. 

  // Determine whether we are near the medial wall. The ?h.cortex.label has been
  // loaded and used to set the "marked2" field in the surfaces. Find the closest
  // vertex to this CRS and see whether it has been marked or not. Might be able
  // to just use the white instead of white and pial. 
  int vtxno, surftype, hemi, mask;
  double dmin;
  MRIS *ClosestSurf = Surf2VolSeg::GetClosestVertexNo(c, r, s, DoLH, DoRH, 1, 1,  0, 0, &vtxno, &surftype, &hemi, &dmin);
  if(ClosestSurf == NULL) return(0);
  mask = ClosestSurf->vertices[vtxno].marked2;
  if(debug) printf("%d %d %d   %d %d %d %g   %d\n",c,r,s,vtxno, surftype, hemi, dmin, mask);

  // It is in the ?h.cortex.label (and not in the medial wall)
  // Relabel to unknown. Could relabel to an extracerebral like CSF
  if(mask) return(RibbonLabel); 

  // It is in the medial wall. This will fill in space between the medial wall and subcort
  // structures with the aseg. It will also prevent little bits of WM from forming on the
  // outside of subcort structures where the surface is not flush (eg, VentralDC).
  return(volsegid);
}

int Surf2VolSeg::RelabelSegVoxWithSurf(MRI *volseg, int c, int r, int s, int LabelType, int *hemi, int *surftype, double *dmin)
{
  *hemi = 0;
  *surftype = 0;
  *dmin = 1e9;

  int volsegid = MRIgetVoxVal(volseg,c,r,s,0);
  if(debug) printf("RelabelSegVoxWithSurf(): LabelType %d volsegid %d, crs %d %d %d\n",LabelType,volsegid,c,r,s);

  int AllowLH=0, AllowRH=0, AllowWhite=1, AllowPial=0;
  int WhiteDotDir = 0, PialDotDir = 0;
  if(LabelType == 1){// Label Cortex
    if(!IS_CORTEX(volsegid)) return(volsegid);
    AllowWhite=1;
    WhiteDotDir = +1;
    AllowPial=1;
    PialDotDir  = -1;
    if(volsegid == Left_Cerebral_Cortex && DoLH){
      AllowLH=1;
      AllowRH=0;
    }
    if(volsegid == Right_Cerebral_Cortex && DoRH){
      AllowLH=0;
      AllowRH=1;
    }
  }
  else { // Label White Matter
    if(!IS_WHITE_CLASS(volsegid)) return(volsegid);
    AllowWhite=1;
    WhiteDotDir = -1;
    AllowPial=0;
    PialDotDir=0;
    if(volsegid == Left_Cerebral_White_Matter  && DoLH){
      AllowLH=1;
      AllowRH=0;
    }
    if(volsegid == Right_Cerebral_White_Matter && DoRH){
      AllowLH=0;
      AllowRH=1;
    }
  }
  if(debug) printf("  Allow LH %d RH %d White %d Pial %d, WDD %d, PDD %d\n",AllowLH, AllowRH, AllowWhite, AllowPial,WhiteDotDir,PialDotDir);

  int vtxno;
  MRIS *ClosestSurf = GetClosestVertexNo(c, r, s, AllowLH, AllowRH, AllowWhite, AllowPial,
					 WhiteDotDir,PialDotDir,&vtxno, surftype, hemi, dmin);
  if(ClosestSurf == NULL) {
    if(debug) printf("  Closest Surf is NULL\n");
    return(0);
  }

  int annot, annotid;
  annot = ClosestSurf->vertices[vtxno].annotation;
  if(ClosestSurf->ct) CTABfindAnnotation(ClosestSurf->ct, annot, &annotid);
  else                annotid = annotation_to_index(annot);
  if(debug) {
    double x = ClosestSurf->vertices[vtxno].x;
    double y = ClosestSurf->vertices[vtxno].y;
    double z = ClosestSurf->vertices[vtxno].z;
    printf("  vtxno %d vtxxyz = [%g,%g,%g] annot %d   annotid %d\n",vtxno,x,y,z,annot,annotid);
  }
  if(annotid == -1) {
    annotid = GetNearestVertexWithAnnot(vtxno, ClosestSurf, nHopsMax);
    if(debug) printf("  nearest annot %d   annotid %d (nHopsMax %d)\n",annot,annotid,nHopsMax);
    if(annotid == -1) annotid = 0;
  }

  return(annotid);
}

MRIS *Surf2VolSeg::GetClosestVertexNo(int c, int r, int s, 
				      int AllowLH, int AllowRH, int AllowWhite, int AllowPial,
				      int WhiteDotDir, int PialDotDir,
				      int *vtxno, int *surftype, int *hemi, double *dmin)
{
  *vtxno = -1;
  *surftype = -1;
  *hemi = -1;
  *dmin = 1e10;

  // Convert this CRS to Surface RAS
  double x,y,z;
  Surf2VolSeg::CRS2SurfRAS(c,r,s,&x,&y,&z);
  if(debug) printf("voxxyz = [%g %g %g]\n",x,y,z);

  // An automatic function to compute the dot product
  auto dotfunc = [](double x, double y, double z, MRIS *surf, int vtxno){
    VERTEX *v = &(surf->vertices[vtxno]);
    double rms, xn, yn, zn, dot, dx,dy,dz;
    dx = (x - v->x); dy = (y - v->y); dz = (z - v->z);
    rms = sqrt(dx*dx+dy*dy*dz*dz);
    xn = dx/rms; yn = dy/rms; zn = dz/rms;
    dot = xn * v->nx + yn * v->ny + zn * v->nz;
    return(dot);
  };

  // Find the closest point on the surfaces
  int lhwvtx=-1,rhwvtx=-1,lhpvtx=-1,rhpvtx=-1;
  float dlhw=1e10,drhw=1e10,dlhp=1e10,drhp=1e10;
  double dot;
  if(AllowLH) {
    if(AllowWhite){
      lhwvtx = MHTfindClosestVertexNoXYZ(lhwhite_hash, lhwhite, x,y,z, &dlhw);
      if(lhwvtx < 0) dlhw = 1e10;
      else if(WhiteDotDir){
	dot = dotfunc(x,y,z,lhwhite,lhwvtx);
	if(dot*WhiteDotDir < 0)  {
	  if(debug) printf("lh white dot check failed dot = %g, vtxno %d \n",dot,lhwvtx);
	  dlhw = MRISfindMinDistanceVertexWithDotCheck(lhwhite, c, r, s, involseg, WhiteDotDir, &lhwvtx) ;
	}
      }
    }
    if(AllowPial){
      lhpvtx = MHTfindClosestVertexNoXYZ(lhpial_hash,  lhpial,  x,y,z, &dlhp);
      if(lhpvtx < 0) dlhp = 1e10;
      else if (PialDotDir){
	dot = dotfunc(x,y,z,lhpial,lhpvtx);
	if (dot*PialDotDir < 0)  {
	  if(debug) printf("lh pial dot check failed dot = %g, vtxno %d\n",dot,lhpvtx);
	  dlhp = MRISfindMinDistanceVertexWithDotCheck(lhpial, c, r, s, involseg, PialDotDir, &lhpvtx) ;
	}
      }
    }
  }
  if(AllowRH) {
    if(AllowWhite){
      rhwvtx = MHTfindClosestVertexNoXYZ(rhwhite_hash, rhwhite, x,y,z, &drhw);
      if(rhwvtx < 0) drhw = 1e10;
      else if(WhiteDotDir){
	dot = dotfunc(x,y,z,rhwhite,rhwvtx);
	if (dot*WhiteDotDir < 0)  {
	  if(debug) printf("rh white dot check failed dot = %g, vtxno %d, d=%g\n",dot,rhwvtx,drhw);
	  drhw = MRISfindMinDistanceVertexWithDotCheck(rhwhite, c, r, s, involseg, WhiteDotDir, &rhwvtx) ;
	  if(debug){
	    dot = dotfunc(x,y,z,rhwhite,rhwvtx);
	    printf("  new dot = %g, vtxno %d, d=%g\n",dot,rhwvtx,drhw);
	  }
	}
      }
    }
    if(AllowPial){
      rhpvtx = MHTfindClosestVertexNoXYZ(rhpial_hash,  rhpial,  x,y,z, &drhp);
      if(rhpvtx < 0) drhp = 1e10;
      else if (PialDotDir){
	dot = dotfunc(x,y,z,rhpial,rhpvtx);
	if (dot*PialDotDir < 0)  {
	  if(debug) printf("rh pial dot check failed dot = %g, vtxno %d, d=%g\n",dot,rhpvtx,drhp);
	  drhp = MRISfindMinDistanceVertexWithDotCheck(rhpial, c, r, s, involseg, PialDotDir, &rhpvtx) ;
	  if(debug){
	    dot = dotfunc(x,y,z,rhpial,rhpvtx);
	    printf("  new dot = %g, vtxno %d, d=%g\n",dot,rhpvtx,drhp);
	  }
	}
      }
    }
  }
  if(debug){
    printf("lhwvtx %d dlhw %g\n",lhwvtx,dlhw);
    printf("lhpvtx %d dlhp %g\n",lhpvtx,dlhp);
    printf("rhwvtx %d drhw %g\n",rhwvtx,drhw);
    printf("rhpvtx %d drhp %g\n",rhpvtx,drhp);
  }

  // Not close to a surface
  if(lhwvtx < 0 && lhpvtx < 0 && rhwvtx < 0 && rhpvtx < 0) return(NULL);

  if(dlhw <= dlhp && dlhw <= drhw && dlhw <= drhp){
    *vtxno = lhwvtx;
    *surftype = GRAY_WHITE;
    *hemi = 1;
    *dmin = dlhw;
    return(lhwhite);
 }
  if(dlhp <= dlhw && dlhp <= drhw && dlhp <= drhp){
    *vtxno = lhpvtx;
    *surftype = GRAY_CSF;
    *hemi = 1;
    *dmin = dlhp;
    return(lhpial);
  }
  if(drhw <= dlhw && drhw <= dlhp && drhw <= drhp){
    *vtxno = rhwvtx;
    *surftype = GRAY_WHITE;
    *hemi = 2;
    *dmin = drhw;
    return(rhwhite);
  }
  if(drhp <= dlhw && drhp <= dlhp && drhp <= drhw){
    *vtxno = rhpvtx;
    *hemi = 2;
    *surftype = GRAY_CSF;
    *dmin = drhp;
    return(rhpial);
  }

  return(NULL); // should never get here
}


int Surf2VolSeg::GetNearestVertexWithAnnot(int CenterVtx, MRI_SURFACE *Surf, int nHops)
{
  if(CenterVtx < 0 || CenterVtx > Surf->nvertices){
    printf("ERROR: GetNearestVertexWithAnnot() vertex %d out of range\n",CenterVtx);
    fflush(stdout);
    exit(1);
  }
  SURFHOPLIST *shl = SetSurfHopList(CenterVtx, Surf, nHops);
  int nthhop,nthnbr,nnbrs,annotid,vtxno,annot;
  for (nthhop = 0; nthhop < nHops; nthhop++) {
    nnbrs = shl->nperhop[nthhop];  // number of neighbrs in the nthhop ring
    // loop through the neighbors nthhop rings away
    for (nthnbr = 0; nthnbr < nnbrs; nthnbr++) {
      vtxno = shl->vtxlist[nthhop][nthnbr];
      annot = Surf->vertices[vtxno].annotation;
      if(Surf->ct) CTABfindAnnotation(Surf->ct, annot, &annotid);
      else         annotid = annotation_to_index(annot);
      //printf("  %2d %2d  %6d %3d\n",nthhop,nthnbr,vtxno,annotid);
      if(annotid != -1){
	//printf("  Found annotid %d vtx %d  %d %d\n",annotid,vtxno,nthhop,nthnbr);
	SurfHopListFree(&shl);
	return(annotid);
      }
    }
  }
  SurfHopListFree(&shl);
  return(-1);
}

int Surf2VolSeg::CRS2SurfRAS(int c, int r, int s, double *x, double *y, double *z)
{
  MATRIX *CRS, *RAS;
  CRS = MatrixAlloc(4,1,MATRIX_REAL);
  CRS->rptr[4][1] = 1;
  RAS = MatrixAlloc(4,1,MATRIX_REAL);
  RAS->rptr[4][1] = 1;
  CRS->rptr[1][1] = c;
  CRS->rptr[2][1] = r;
  CRS->rptr[3][1] = s;
  RAS = MatrixMultiply(Vox2RAS,CRS,RAS);
  *x = RAS->rptr[1][1];
  *y = RAS->rptr[2][1];
  *z = RAS->rptr[3][1];
  MatrixFree(&CRS);
  MatrixFree(&RAS);
  return(0);
}

void Surf2VolSeg::Free(void){
  printf("Starting Surf2VolSeg free\n");
  if(outvolseg) MRIfree(&outvolseg);
  if(involseg) MRIfree(&involseg);
  if(ribbon) MRIfree(&ribbon);
  if(lhwhite_hash) MHTfree(&lhwhite_hash);
  if(rhwhite_hash) MHTfree(&rhwhite_hash);
  if(lhpial_hash) MHTfree(&lhpial_hash);
  if(rhpial_hash) MHTfree(&rhpial_hash);
  if(lhwhite) MRISfree(&lhwhite);
  if(rhwhite) MRISfree(&rhwhite);
  if(lhpial) MRISfree(&lhpial);
  if(rhpial) MRISfree(&rhpial);
  printf("free done\n");
  fflush(stdout);
}

int Surf2VolSeg::SetSubjectPaths(char *subject, char *SD){
  string SUBJECTS_DIR;
  if(SD==NULL) SUBJECTS_DIR = getenv("SUBJECTS_DIR");
  else SUBJECTS_DIR = SD;
  involsegpath = SUBJECTS_DIR + "/" + subject + "/mri/aseg.presurf.hypos.mgz" ;
  ribbonpath   = SUBJECTS_DIR + "/" + subject + "/mri/ribbon.mgz" ;
  lhwhitepath  = SUBJECTS_DIR + "/" + subject + "/surf/lh.white" ;
  lhcortexlabelpath = SUBJECTS_DIR + "/" + subject + "/label/lh.cortex.label" ;
  lhpialpath  = SUBJECTS_DIR + "/" + subject + "/surf/lh.pial" ;
  rhwhitepath  = SUBJECTS_DIR + "/" + subject + "/surf/rh.white" ;
  rhpialpath  = SUBJECTS_DIR + "/" + subject + "/surf/rh.pial" ;
  rhcortexlabelpath = SUBJECTS_DIR + "/" + subject + "/label/rh.cortex.label" ;
  if(LabelCortex){
    lhannotpath = SUBJECTS_DIR + "/" + subject + "/label/" + lhannotname;
    rhannotpath = SUBJECTS_DIR + "/" + subject + "/label/" + rhannotname;
  }
  return(0);
}

int Surf2VolSeg::LoadData(void){
  printf("Loading %s\n",involsegpath.c_str());
  involseg = MRIread(involsegpath.c_str());
  if(outvolseg == NULL){
    outvolseg = MRIalloc(involseg->width, involseg->height, involseg->depth, MRI_INT);
    MRIcopyHeader(involseg, outvolseg);
    outvolseg->ct = CTABreadDefault(); // not sure this will be right always (coult cmd spec)
  }
  if(involseg==NULL) exit(1);
  Vox2RAS = MRIxfmCRS2XYZtkreg(involseg);
  if(ribbonpath.length()){
    printf("Loading %s\n",ribbonpath.c_str());
    ribbon = MRIread(ribbonpath.c_str());
    if(ribbon==NULL) exit(1);
  }
  if(DoLH){
    printf("Loading %s\n",lhwhitepath.c_str());
    lhwhite = MRISread(lhwhitepath.c_str());
    if(lhwhite==NULL) exit(1);
    printf("Loading %s\n",lhpialpath.c_str());
    lhpial  = MRISread(lhpialpath.c_str());
    if(lhpial==NULL) exit(1);
    lhwhite_hash = MHTcreateVertexTable_Resolution(lhwhite, CURRENT_VERTICES,hashres);
    lhpial_hash = MHTcreateVertexTable_Resolution(lhpial, CURRENT_VERTICES,hashres);
    printf("Loading %s\n",lhcortexlabelpath.c_str());
    LABEL *cortexlabel = LabelRead(NULL, &(lhcortexlabelpath.c_str()[0]));
    if(cortexlabel==NULL) exit(1);
    LabelMark2(cortexlabel,lhwhite);
    LabelMark2(cortexlabel,lhpial);
    LabelFree(&cortexlabel);
    if(RipUnknown)  {
      printf("Ripping lh vertices labeled not in lh.cortex.label\n");
      int nripped = 0, vtxno;
      for (vtxno = 0; vtxno < lhwhite->nvertices; vtxno++) {
	if(lhwhite->vertices[vtxno].marked2 == 0){
	  lhwhite->vertices[vtxno].ripflag = 1;
	  lhpial->vertices[vtxno].ripflag  = 1;
	  nripped++;
	}
      }
      printf("  ripped %d vertices from lh hemi\n",nripped);
    }
    if(lhannotpath.length()){
      printf("Loading %s\n",lhannotpath.c_str());
      int err = MRISreadAnnotation(lhwhite, lhannotpath.c_str());
      if(err)  {
	printf("ERROR: MRISreadAnnotation() white failed %s\n",lhannotpath.c_str());
	exit(1);
      }
      err = MRISreadAnnotation(lhpial, lhannotpath.c_str());
      if(err)  {
	printf("ERROR: MRISreadAnnotation() pial failed %s\n",lhannotpath.c_str());
	exit(1);
      }
    }
  }
  if(DoRH){
    printf("Loading %s\n",rhwhitepath.c_str());
    rhwhite = MRISread(rhwhitepath.c_str());
    if(rhwhite==NULL) exit(1);
    printf("Loading %s\n",rhpialpath.c_str());
    rhpial  = MRISread(rhpialpath.c_str());
    if(rhpial==NULL) exit(1);
    rhwhite_hash = MHTcreateVertexTable_Resolution(rhwhite, CURRENT_VERTICES,hashres);
    rhpial_hash = MHTcreateVertexTable_Resolution(rhpial, CURRENT_VERTICES,hashres);
    printf("Loading %s\n",rhcortexlabelpath.c_str());
    LABEL *cortexlabel = LabelRead(NULL, &(rhcortexlabelpath.c_str()[0]));
    if(cortexlabel==NULL) exit(1);
    LabelMark2(cortexlabel,rhwhite);
    LabelMark2(cortexlabel,rhpial);
    LabelFree(&cortexlabel);
    if(RipUnknown)  {
      printf("Ripping rh vertices labeled not in rh.cortex.label\n");
      int nripped = 0, vtxno;
      for (vtxno = 0; vtxno < rhwhite->nvertices; vtxno++) {
	if(rhwhite->vertices[vtxno].marked2 == 0){
	  rhwhite->vertices[vtxno].ripflag = 1;
	  rhpial->vertices[vtxno].ripflag  = 1;
	  nripped++;
	}
      }
      printf("  ripped %d vertices from rh hemi\n",nripped);
    }
    if(rhannotpath.length()){
      printf("Loading %s\n",rhannotpath.c_str());
      int err = MRISreadAnnotation(rhwhite, rhannotpath.c_str());
      if(err)  {
	printf("ERROR: MRISreadAnnotation() white failed %s\n",rhannotpath.c_str());
	exit(1);
      }
      err = MRISreadAnnotation(rhpial, rhannotpath.c_str());
      if(err)  {
	printf("ERROR: MRISreadAnnotation() pial failed %s\n",rhannotpath.c_str());
	exit(1);
      }
    }
  }
  printf("Done loading\n");
  fflush(stdout);
  return(0);
}