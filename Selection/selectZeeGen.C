//================================================================================================
//
// Select Z->ee candidates
//
//  * outputs ROOT files of events passing selection
//
//________________________________________________________________________________________________
#if !defined(__CINT__) || defined(__MAKECINT__)
#include <TROOT.h>                  // access to gROOT, entry point to ROOT system
#include <TSystem.h>                // interface to OS
#include <TFile.h>                  // file handle class
#include <TTree.h>                  // class to access ntuples
#include <TClonesArray.h>           // ROOT array class
#include <TBenchmark.h>             // class to track macro running statistics
#include <TVector2.h>               // 2D vector class
#include <TMath.h>                  // ROOT math library
#include <vector>                   // STL vector class
#include <iostream>                 // standard I/O
#include <iomanip>                  // functions to format standard I/O
#include <fstream>                  // functions for file I/O
#include "TLorentzVector.h"         // 4-vector class
#include "TH1D.h"
#include "TRandom.h"

#include "ConfParse.hh"             // input conf file parser
#include "../Utils/CSample.hh"      // helper class to handle samples
#include "../Utils/LeptonCorr.hh"   // electron scale and resolution corrections

// define structures to read in ntuple
#include "BaconAna/DataFormats/interface/BaconAnaDefs.hh"
#include "BaconAna/DataFormats/interface/TEventInfo.hh"
#include "BaconAna/DataFormats/interface/TGenEventInfo.hh"
#include "BaconAna/DataFormats/interface/TGenParticle.hh"
#include "BaconAna/DataFormats/interface/TElectron.hh"
#include "BaconAna/DataFormats/interface/TPhoton.hh"
#include "BaconAna/DataFormats/interface/TVertex.hh"
#include "BaconAna/Utils/interface/TTrigger.hh"

// lumi section selection with JSON files
#include "BaconAna/Utils/interface/RunLumiRangeMap.hh"

#include "../Utils/LeptonIDCuts.hh" // helper functions for lepton ID selection
#include "../Utils/MyTools.hh"      // various helper functions
#endif

//=== MAIN MACRO ================================================================================================= 

void selectZeeGen(const TString conf="zee.conf", // input file
		  const TString outputDir="."   // output directory
		  ) {
  gBenchmark->Start("selectZeeGen");

  //--------------------------------------------------------------------------------------------------------------
  // Settings 
  //============================================================================================================== 
 
  const Double_t MASS_LOW  = 40;
  const Double_t MASS_HIGH = 200;
  const Double_t PT_CUT    = 22;
  const Double_t ETA_CUT   = 2.5;
  const Double_t ELE_MASS  = 0.000511;
  
  const Double_t ECAL_GAP_LOW  = 1.4442;
  const Double_t ECAL_GAP_HIGH = 1.566;

  const Double_t escaleNbins  = 2;
  const Double_t escaleEta[]  = { 1.4442, 2.5   };
  const Double_t escaleCorr[] = { 0.992,  1.009 };

  const Int_t BOSON_ID  = 23;
  const Int_t LEPTON_ID = 11;

  // load trigger menu
  const baconhep::TTrigger triggerMenu("../../BaconAna/DataFormats/data/HLT_50nsGRun");

  // load pileup reweighting file
  TFile *f_rw = TFile::Open("../Tools/pileup_rw_baconDY.root", "read");

  // for systematics we need 3
  TH1D *h_rw = (TH1D*) f_rw->Get("h_rw_golden");
  TH1D *h_rw_up = (TH1D*) f_rw->Get("h_rw_up_golden");
  TH1D *h_rw_down = (TH1D*) f_rw->Get("h_rw_down_golden"); 

  if (h_rw==NULL) cout<<"WARNIG h_rw == NULL"<<endl;
  if (h_rw_up==NULL) cout<<"WARNIG h_rw == NULL"<<endl;
  if (h_rw_down==NULL) cout<<"WARNIG h_rw == NULL"<<endl;

  //--------------------------------------------------------------------------------------------------------------
  // Main analysis code 
  //==============================================================================================================  

    
  vector<TString>  snamev;      // sample name (for output files)  
  vector<CSample*> samplev;     // data/MC samples
  
  //
  // parse .conf file
  //
  confParse(conf, snamev, samplev);
   
  // Create output directory
  gSystem->mkdir(outputDir,kTRUE);
  const TString ntupDir = outputDir + TString("/ntuples");
  gSystem->mkdir(ntupDir,kTRUE);
  
  //
  // Declare output ntuple variables
  //
  UInt_t  runNum, lumiSec, evtNum;
  UInt_t  matchGen;
  UInt_t  npv, npu;
  UInt_t  triggerDec;
  UInt_t  goodPV;
  UInt_t  matchTrigger;
  UInt_t  ngenlep;
  TLorentzVector *genlep1=0, *genlep2=0;
  Int_t   genq1, genq2;
  UInt_t nlep;
  TLorentzVector *lep1=0, *lep2=0;
  TLorentzVector *sc1=0, *sc2=0;
  Int_t   q1, q2;
  Float_t scale1fbGen,scale1fb,scale1fbUp,scale1fbDown;

  std::vector<float> *lheweight = new std::vector<float>();
  
  // Data structures to store info from TTrees
  baconhep::TEventInfo *info   = new baconhep::TEventInfo();
  baconhep::TGenEventInfo *gen = new baconhep::TGenEventInfo();
  TClonesArray *genPartArr     = new TClonesArray("baconhep::TGenParticle");
  TClonesArray *electronArr    = new TClonesArray("baconhep::TElectron");
  TClonesArray *scArr          = new TClonesArray("baconhep::TPhoton");
  TClonesArray *vertexArr      = new TClonesArray("baconhep::TVertex");
  
  TFile *infile=0;
  TTree *eventTree=0;
  
  //
  // loop over samples
  //  
  for(UInt_t isam=0; isam<samplev.size(); isam++) {
    
    // Assume signal sample is given name "zee" - flag to store GEN Z kinematics
    Bool_t isSignal = (snamev[isam].Contains("zee",TString::kIgnoreCase));
  
    // flag to reject Z->ee events when selecting at wrong-flavor background events
    Bool_t isWrongFlavor = (snamev[isam].CompareTo("zxx",TString::kIgnoreCase)==0);  
    
    CSample* samp = samplev[isam];
  
    //
    // Set up output ntuple
    //
    TString outfilename = ntupDir + TString("/") + snamev[isam] + TString("_select.raw.root");
    TFile *outFile = new TFile(outfilename,"RECREATE"); 
    TTree *outTree = new TTree("Events","Events");
    outTree->Branch("runNum",     &runNum,     "runNum/i");      // event run number
    outTree->Branch("lumiSec",    &lumiSec,    "lumiSec/i");     // event lumi section
    outTree->Branch("evtNum",     &evtNum,     "evtNum/i");      // event number
    outTree->Branch("matchGen",   &matchGen,   "matchGen/i");    // event has both leptons matched to MC Z->ll
    outTree->Branch("npv",        &npv,        "npv/i");         // number of primary vertices
    outTree->Branch("npu",        &npu,        "npu/i");         // number of in-time PU events (MC)
    outTree->Branch("triggerDec",   &triggerDec,   "triggerDec/i");    // event pass the trigger
    outTree->Branch("goodPV",   &goodPV,   "goodPV/i");    // event has a good PV
    outTree->Branch("matchTrigger",   &matchTrigger,   "matchTrigger/i");    // event has at least one lepton matched to the trigger
    outTree->Branch("ngenlep",     &ngenlep,     "ngenlep/i");      // number of gen leptons
    outTree->Branch("genlep1",   "TLorentzVector",  &genlep1);     // gen lepton1 4-vector
    outTree->Branch("genlep2",   "TLorentzVector",  &genlep2);     // gen lepton2 4-vector
    outTree->Branch("genq1",          &genq1,         "genq1/I");          // charge of lepton1
    outTree->Branch("genq2",          &genq2,         "genq2/I");          // charge of lepton2
    outTree->Branch("nlep",     &nlep,     "nlep/i");      // number of leptons
    outTree->Branch("lep1",       "TLorentzVector",  &lep1);     // lepton1 4-vector
    outTree->Branch("lep2",       "TLorentzVector",  &lep2);     // lepton2 4-vector
    outTree->Branch("sc1",       "TLorentzVector",  &sc1);       // tag supercluster 4-vector
    outTree->Branch("sc2",       "TLorentzVector",  &sc2);       // probe supercluster 4-vector
    outTree->Branch("q1",          &q1,         "q1/I");          // charge of lepton1
    outTree->Branch("q2",          &q2,         "q2/I");          // charge of lepton2
    outTree->Branch("scale1fbGen",   &scale1fbGen,   "scale1fbGen/F");    // event weight per 1/fb (MC)
    outTree->Branch("scale1fb",   &scale1fb,   "scale1fb/F");    // event weight per 1/fb (MC)
    outTree->Branch("scale1fbUp",    &scale1fbUp,   "scale1fbUp/F");    // event weight per 1/fb (MC)
    outTree->Branch("scale1fbDown",    &scale1fbDown,   "scale1fbDown/F");    // event weight per 1/fb (MC)
    outTree->Branch("lheweight",  &lheweight);

    //
    // loop through files
    //
    const UInt_t nfiles = samp->fnamev.size();
    for(UInt_t ifile=0; ifile<nfiles; ifile++) {  

      // Read input file and get the TTrees
      cout << "Processing " << samp->fnamev[ifile] << " [xsec = " << samp->xsecv[ifile] << " pb] ... " << endl; cout.flush();
      infile = TFile::Open(samp->fnamev[ifile]); 
      assert(infile);

      eventTree = (TTree*)infile->Get("Events");
      assert(eventTree);  
      eventTree->SetBranchAddress("Info",     &info);        TBranch *infoBr     = eventTree->GetBranch("Info");
      eventTree->SetBranchAddress("Electron", &electronArr); TBranch *electronBr = eventTree->GetBranch("Electron");
      eventTree->SetBranchAddress("Photon",   &scArr);       TBranch *scBr       = eventTree->GetBranch("Photon");
      eventTree->SetBranchAddress("PV",   &vertexArr);       TBranch *vertexBr = eventTree->GetBranch("PV");
      Bool_t hasGen = eventTree->GetBranchStatus("GenEvtInfo");
      TBranch *genBr=0, *genPartBr=0;
      if(hasGen) {
        eventTree->SetBranchAddress("GenEvtInfo", &gen); genBr = eventTree->GetBranch("GenEvtInfo");
	eventTree->SetBranchAddress("GenParticle",&genPartArr); genPartBr = eventTree->GetBranch("GenParticle");
      }

      // Compute MC event weight per 1/fb
      const Double_t xsec = samp->xsecv[ifile];
      Double_t totalWeightGen=0;
      Double_t totalWeight=0;
      Double_t totalWeightUp=0;
      Double_t totalWeightDown=0;
      Double_t puWeight=0;
      Double_t puWeightUp=0;
      Double_t puWeightDown=0;

      if (hasGen) {
	for(UInt_t ientry=0; ientry<eventTree->GetEntries(); ientry++) {
	  infoBr->GetEntry(ientry);
	  genBr->GetEntry(ientry);
	  puWeight = h_rw->GetBinContent(h_rw->FindBin(info->nPUmean));
	  puWeightUp = h_rw_up->GetBinContent(h_rw_up->FindBin(info->nPUmean));
	  puWeightDown = h_rw_down->GetBinContent(h_rw_down->FindBin(info->nPUmean));
	  totalWeightGen+=gen->weight;
	  totalWeight+=gen->weight*puWeight;
	  totalWeightUp+=gen->weight*puWeightUp;
	  totalWeightDown+=gen->weight*puWeightDown;
	}
      }
     
      
      //
      // loop over events
      //
      Double_t nsel=0, nselvar=0;
      for(UInt_t ientry=0; ientry<eventTree->GetEntries(); ientry++) {
	infoBr->GetEntry(ientry);

        if(ientry%1000000==0) cout << "Processing event " << ientry << ". " << (double)ientry/(double)eventTree->GetEntries()*100 << " percent done with this file." << endl;

	Double_t weightGen=1;
	Double_t weight=1;
	Double_t weightUp=1;
	Double_t weightDown=1;
	if(xsec>0 && totalWeightGen>0) weightGen = xsec/totalWeightGen;
        if(xsec>0 && totalWeight>0) weight = xsec/totalWeight;
	if(xsec>0 && totalWeightUp>0) weightUp = xsec/totalWeightUp;
	if(xsec>0 && totalWeightDown>0) weightDown = xsec/totalWeightDown;
	if(hasGen) {
	  genPartArr->Clear();
	  genBr->GetEntry(ientry);
          genPartBr->GetEntry(ientry);
	  puWeight = h_rw->GetBinContent(h_rw->FindBin(info->nPUmean));
	  puWeightUp = h_rw_up->GetBinContent(h_rw_up->FindBin(info->nPUmean));
	  puWeightDown = h_rw_down->GetBinContent(h_rw_down->FindBin(info->nPUmean));
	  weightGen*=gen->weight;
	  weight*=gen->weight*puWeight;
	  weightUp*=gen->weight*puWeightUp;
	  weightDown*=gen->weight*puWeightDown;
	}

	// veto z -> xx decays for signal and z -> ee for bacground samples (needed for inclusive DYToLL sample)
	if (isWrongFlavor && hasGen && fabs(toolbox::flavor(genPartArr, BOSON_ID))==LEPTON_ID) continue;
	else if (isSignal && hasGen && fabs(toolbox::flavor(genPartArr, BOSON_ID))!=LEPTON_ID) continue;

	// trigger requirement
	Bool_t passElTrigger = kFALSE;
	if (isEleTrigger(triggerMenu, info->triggerBits, 0)) passElTrigger=kTRUE;
	
        // good vertex requirement
	Bool_t hasGoodPV = kFALSE;
        if((info->hasGoodPV)) hasGoodPV=kTRUE;

	electronArr->Clear();
        electronBr->GetEntry(ientry);
	scArr->Clear();
	scBr->GetEntry(ientry);

	Int_t n_el=0;
	TLorentzVector vlep1(0., 0., 0., 0.);
	TLorentzVector vlep2(0., 0., 0., 0.);
	TLorentzVector vsc1(0., 0., 0., 0.);
	TLorentzVector vsc2(0., 0., 0., 0.);
	Bool_t hasTriggerMatch = kFALSE;
	for(Int_t i=0; i<electronArr->GetEntriesFast(); i++) {
	  const baconhep::TElectron *el = (baconhep::TElectron*)((*electronArr)[i]);
	  // apply scale and resolution corrections to MC
          Double_t elscEt_corr = el->scEt;
          	  
	  if(elscEt_corr        < PT_CUT)     continue;  // lepton pT cut
	  if(fabs(el->scEta)    > ETA_CUT)    continue;  // lepton |eta| cut
	  if(fabs(el->scEta)>=ECAL_GAP_LOW && fabs(el->scEta)<=ECAL_GAP_HIGH) continue; // check ECAL gap
	  if(!passEleID(el,info->rhoIso))     continue;  // lepton selection

	  if(isEleTriggerObj(triggerMenu, el->hltMatchBits, kFALSE, 0)) hasTriggerMatch=kTRUE;

	  double El_Pt=el->pt;
	     
	  if(El_Pt>vlep1.Pt())
	    {
	      vlep2=vlep1;
	      vlep1.SetPtEtaPhiM(El_Pt, el->eta, el->phi, ELE_MASS);
	      vsc1.SetPtEtaPhiM(el->scEt, el->scEta, el->scPhi, ELE_MASS);
	      q2=q1;
	      q1=el->q;
	    }
	  else if(El_Pt<=vlep1.Pt()&&El_Pt>vlep2.Pt())
	    {
	      vlep2.SetPtEtaPhiM(El_Pt, el->eta, el->phi, ELE_MASS);
	      vsc2.SetPtEtaPhiM(el->scEt, el->scEta, el->scPhi, ELE_MASS);
	      q2=el->q;
	    }
	 
	  n_el++;
	}

	Int_t n_genel=0;
	Int_t glepq1=-99;
	Int_t glepq2=-99;
	TLorentzVector *gvec=new TLorentzVector(0,0,0,0);
	TLorentzVector *glep1=new TLorentzVector(0,0,0,0);
	TLorentzVector *glep2=new TLorentzVector(0,0,0,0);
	TLorentzVector *gph=new TLorentzVector(0,0,0,0);
	Bool_t hasGenMatch = kFALSE;
	if(isSignal && hasGen) {
	  toolbox::fillGen(genPartArr, BOSON_ID, gvec, glep1, glep2,&glepq1,&glepq2,1);

	  Bool_t match1 = ( ((glep1) && toolbox::deltaR(vlep1.Eta(), vlep1.Phi(), glep1->Eta(), glep1->Phi())<0.3) || ((glep2) && toolbox::deltaR(vlep1.Eta(), vlep1.Phi(), glep2->Eta(), glep2->Phi())<0.3) );
	  Bool_t match2 = ( ((glep1) && toolbox::deltaR(vlep2.Eta(), vlep2.Phi(), glep1->Eta(), glep1->Phi())<0.3) || ((glep2) && toolbox::deltaR(vlep2.Eta(), vlep2.Phi(), glep2->Eta(), glep2->Phi())<0.3) );

	  for(Int_t i=0; i<genPartArr->GetEntriesFast(); i++) {
	    const baconhep::TGenParticle* genloop = (baconhep::TGenParticle*) ((*genPartArr)[i]);
	    if(fabs(genloop->pdgId)!=22) continue;
	    gph->SetPtEtaPhiM(genloop->pt, genloop->eta, genloop->phi, genloop->mass);
	    if(toolbox::deltaR(gph->Eta(),gph->Phi(),glep1->Eta(),glep1->Phi())<0.1)
	      {
		glep1->operator+=(*gph);
	      }
	    if(toolbox::deltaR(gph->Eta(),gph->Phi(),glep2->Eta(),glep2->Phi())<0.1)
	      {
		glep2->operator+=(*gph);
	      }
	  }

	  if(glep1->Pt() >= PT_CUT && fabs(glep1->Eta())< ETA_CUT && (fabs(glep1->Eta())<ECAL_GAP_LOW || fabs(glep1->Eta())>ECAL_GAP_HIGH))
	    {
	      genlep1=glep1;
	      genq1=glepq1;
	      n_genel++;
	    }
	  if(glep2->Pt() >= PT_CUT && fabs(glep2->Eta())< ETA_CUT && (fabs(glep2->Eta())<ECAL_GAP_LOW || fabs(glep2->Eta())>ECAL_GAP_HIGH))
	    {
	      genlep2=glep2;
	      genq2=glepq2;
	      n_genel++;
	    }
	  
	  if(match1 && match2) hasGenMatch = kTRUE;
	}

	//
	// Fill tree
	//
	runNum   = info->runNum;
	lumiSec  = info->lumiSec;
	evtNum   = info->evtNum;

	if (hasGenMatch) matchGen=1;
	else matchGen=0;
	if (passElTrigger) triggerDec=1;
	else triggerDec=0;
	if (hasGoodPV) goodPV=1;
	else goodPV=0;
	if (hasTriggerMatch) matchTrigger=1;
	else matchTrigger=0;
	
	vertexArr->Clear();
	vertexBr->GetEntry(ientry);
	
	npv      = vertexArr->GetEntries();
	npu      = info->nPUmean;
	scale1fbGen = weightGen;
	scale1fb = weight;
	scale1fbUp = weightUp;
	scale1fbDown = weightDown;

	lheweight->clear();
	for (int j = 0; j<=110; j++)
	  {
	    lheweight->push_back(gen->lheweight[j]);
	  }

	ngenlep=n_genel;

	nlep=n_el;
	lep1=&vlep1;
        lep2=&vlep2;
	sc1=&vsc1;
        sc2=&vsc2;
	
	nsel+=weight;
	nselvar+=weight*weight;

	outTree->Fill();

	delete gvec;
	delete glep1;
	delete glep2;
	delete gph;
	lep1=0, lep2=0;
	sc1=0, sc2=0;
	genlep1=0, genlep2=0;
      }
      delete infile;
      infile=0, eventTree=0;    
      
      cout << nsel  << " +/- " << sqrt(nselvar);
      cout << endl;
    }
    outFile->Write();
    outFile->Close(); 
  }
  delete h_rw;
  delete h_rw_up;
  delete h_rw_down;
  delete f_rw;
  delete info;
  delete gen;
  delete genPartArr;
  delete electronArr;
  delete scArr;
  delete vertexArr;
    
  //--------------------------------------------------------------------------------------------------------------
  // Output
  //==============================================================================================================
  
  cout << endl;
  cout << "  <> Output saved in " << outputDir << "/" << endl;    
  cout << endl;  
      
  gBenchmark->Show("selectZeeGen"); 
}
