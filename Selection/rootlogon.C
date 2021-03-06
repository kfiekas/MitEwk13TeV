{    
  if(gSystem->Getenv("CMSSW_VERSION")) {    
    // TString rfitpath("/afs/cern.ch/cms/slc6_amd64_gcc481/lcg/roofit/5.34.18-cms9/include/");
    // TString path = gSystem->GetIncludePath();
    // path += "-I. -I$ROOTSYS/src -I";
    // path += rfitpath;
    // gSystem->SetIncludePath(path.Data());
    // 
    // TString str = gSystem->GetMakeSharedLib();
    // if (str.Contains("-m32")==0 && str.Contains("-m64")==0) {
    //   str.ReplaceAll("g++", "g++ -m32");
    //   gSystem->SetMakeSharedLib(str);
    // }
    
    gSystem->Load("$CMSSW_BASE/lib/$SCRAM_ARCH/libBaconAnaDataFormats.so");
    
    gROOT->Macro("$CMSSW_BASE/src/BaconAna/macros/setRootEnv.C+");

    // turn on open mp not CINT only ACLIC
    //#ifndef __CINT__
    // TString cmd(gSystem->GetMakeSharedLib());
    // cmd.ReplaceAll("g++","g++ -fopenmp");
    // gSystem->SetMakeSharedLib(cmd);
    //#endif
  
  }
               
  // Show which process needs debugging
  gInterpreter->ProcessLine(".! ps |grep root.exe");
}
