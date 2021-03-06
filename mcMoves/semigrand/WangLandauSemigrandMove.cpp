/*
* Simpatico - Simulation Package for Polymeric and Molecular Liquids
*
* Copyright 2010 - 2014, The Regents of the University of Minnesota
* Distributed under the terms of the GNU General Public License.
*/

#include "WangLandauSemigrandMove.h"
#include <mcMd/simulation/Simulation.h>
#include <mcMd/mcSimulation/McSystem.h>
#ifndef INTER_NOPAIR
#include <mcMd/potentials/pair/McPairPotential.h>
#endif
#include <mcMd/species/LinearSG.h>

#include <mcMd/chemistry/Molecule.h> //testing
#include <util/global.h>

namespace McMd
{

   using namespace Util;

   /* 
   * Constructor
   */
   WangLandauSemigrandMove::WangLandauSemigrandMove(McSystem& system) : 
      SystemMove(system),
      speciesId_(-1),
      mutatorPtr_(0),
      outputFileName_(),
      initialWeightFileName_("0"),
      stepCount_(0)
   {  setClassName("WangLandauAdaptiveStepMove"); } 
   
   /* 
   * Read parameter speciesId.
   */
   void WangLandauSemigrandMove::readParameters(std::istream& in) 
   {
     // Read parameters
     readProbability(in);
     read<int>(in, "speciesId", speciesId_);
     // Cast the Species to HomopolymerSG
     speciesPtr_ = dynamic_cast<LinearSG*>(&(simulation().species(speciesId_)));
     if (!speciesPtr_) {
       UTIL_THROW("Error: Species must be LinearSG");
     }
     read<int>(in, "upperLimit", uLimit_);
     read<int>(in, "lowerLimit", lLimit_);
     if (uLimit_-lLimit_ < 0) {
       UTIL_THROW("Error: Total range is negative");
     }
     capacity_ = speciesPtr_->capacity()+1;
     
     mutatorPtr_ = &speciesPtr_->mutator();
     weights_.allocate(capacity_);
     stateCount_.allocate(capacity_);      
     read<double>(in, "weightStep", weightSize_);
     read<std::string>(in, "outputFileName",outputFileName_);
     read<double>(in,"flatnessCriteria",crit_);
     readOptional<std::string>(in, "initialWeights",initialWeightFileName_);
     std::ifstream weightFile;
     if (initialWeightFileName_!="0") {
       system().fileMaster().open(initialWeightFileName_, weightFile);
       int n;
       double m;
       while (weightFile >> n >>m) {
         weights_[n]= m;
       }
     } else {
         for (int x = 0; x < capacity_; ++x) {
           weights_[x]=0;
         }
       }
     std::string fileName = outputFileName_;
     for (int x = 0; x < capacity_; ++x) {
       stateCount_[x] = 0; 
     }
     fileName = outputFileName_+".weights";      
     system().fileMaster().openOutputFile(fileName, outputFile_);
     outputFile_ << stepCount_ << "	" << weightSize_ << std::endl; 
   }
   /*
   * Load state from an archive.
   */
   void WangLandauSemigrandMove::loadParameters(Serializable::IArchive& ar)
   {  
     McMove::loadParameters(ar);
     loadParameter<int>(ar, "speciesId", speciesId_);
     speciesPtr_ = dynamic_cast<LinearSG*>(&(simulation().species(speciesId_)));
     if (!speciesPtr_) {
       UTIL_THROW("Error: Species must be LinearSG");
     }
     mutatorPtr_ = &speciesPtr_->mutator();
     loadParameter<int>(ar, "upperLimit", uLimit_);
     loadParameter<int>(ar, "lowerLimit", lLimit_);
     loadParameter<double>(ar, "weightStep", weightSize_); 
     loadParameter<std::string>(ar, "outputFileName",outputFileName_);
     loadParameter<double>(ar,"flatnessCriteria",crit_);
     ar & weights_;
     ar & stateCount_;
   }

   /*
   * Save state to an archive.
   */
   void WangLandauSemigrandMove::save(Serializable::OArchive& ar)
   {
     McMove::save(ar);
     ar << speciesId_; 
     ar << uLimit_;
     ar << lLimit_;
     ar << weightSize_;
     ar << outputFileName_;
     ar << crit_;
     ar << capacity_; 
     ar << weights_;
     ar << stateCount_;
   }
   // Determine if it is time to adapt the step size and if so do such
   void WangLandauSemigrandMove::stepAdapt()
   { 
     // Determine if the histogram is sufficiently flat
     bool flat = true;
     int movesMade = 0;
     for (int y = lLimit_; y <= uLimit_; ++y) {
       movesMade = movesMade + stateCount_[y];
     }
     double binAve = movesMade/(uLimit_-lLimit_+1);
     
     for (int z = lLimit_; z <= uLimit_; ++z) {
       if (std::abs(stateCount_[z]-binAve)/binAve > crit_) {
       flat = false;
       }
     }

     // If so Adapt and clear the histogram
     if (flat) {
       crit_=crit_/2;
       weightSize_ = pow(weightSize_, .5);
       outputFile_ << stepCount_ << "	    " << weightSize_ << std::endl;
       for (int x = 0; x < capacity_; ++x) {
         stateCount_[x] = 0;
       }
     }
   }

   Molecule& WangLandauSemigrandMove::randomSGMolecule(int speciesId, int nSubType, int flipType, bool atBoundary)
   {
      if (!atBoundary) {
        return system().randomMolecule(speciesId_);
      }
      int moleculeId,nMol,index,type;
      int count = 0;
      nMol = system().nMolecule(speciesId);
      if (nMol <= 0) {
        Log::file() << "Number of molecules in species " << speciesId
                     << " = " << nMol << std::endl;
        UTIL_THROW("Number of molecules in species <= 0");
      }
      index = simulation().random().uniformInt(0, nSubType);
      for (int i=0; i<nMol; ++i) {
        type = speciesPtr_->mutator().moleculeStateId(system().molecule(speciesId, i));
        if (type==flipType) {
          if (count==index) {
            moleculeId = i;
            return system().molecule(speciesId, moleculeId);
          }
          count += 1;
        }
      }
      UTIL_THROW("No molecule selected");
   }

   /* 
   * Generate, attempt and accept or reject a Monte Carlo move.
   */
   bool WangLandauSemigrandMove::move() 
   {  
     stepCount_=stepCount_+1;
     incrementNAttempt();
     // Special semigrand selector
     bool atBound,wasAtUpperBound, wasAtLowerBound = false;
     double comboPrefactor;
     int flipTypeCapacity;
     int flipType;
     int oldState = mutatorPtr_->stateOccupancy(0);
     int nMolecule = system().nMolecule(speciesId_);
     if  (oldState == uLimit_) {
        atBound=true;
        wasAtUpperBound=true;
        flipType = 0;
        flipTypeCapacity =  mutatorPtr_->stateOccupancy(0);
     }
     if (oldState == lLimit_) {
        atBound = true;
        wasAtLowerBound=true;
        flipType = 1;
        flipTypeCapacity =  mutatorPtr_->stateOccupancy(1);
     }
     Molecule& molecule = randomSGMolecule(speciesId_, oldState, flipType, atBound);
     #ifndef INTER_NOPAIR
     // Calculate pair energy for the chosen molecule
     double oldEnergy = system().pairPotential().moleculeEnergy(molecule);
     #endif
     // Toggle state of the molecule
     int oldStateId = speciesPtr_->mutator().moleculeStateId(molecule);
     int newStateId = (oldStateId == 0) ? 1 : 0;
     speciesPtr_->mutator().setMoleculeState(molecule, newStateId);
     int stateChange = -1;
     if  (newStateId == 0) {
       stateChange = 1;
     }
     #ifdef INTER_NOPAIR 
     bool accept = true;
 
     #else // ifndef INTER_NOPAIR

     // Recalculate pair energy for the molecule
     double newEnergy = system().pairPotential().moleculeEnergy(molecule);
     // Decide whether to accept or reject
     int newState = mutatorPtr_->stateOccupancy(0);
     // Different move if the move is with in the desired range or not
     //int    oldState = newState - stateChange;
     double oldWeight = weights_[oldState];
     double newWeight = weights_[newState];
     // If the system was at either the upper or lower bound calculate the combinatorial prefactor necessary for detailed balance
     if (wasAtLowerBound) {
       comboPrefactor = (double)(capacity_-1-lLimit_)/(double)(capacity_-1);
     }
     if (wasAtUpperBound) {
       comboPrefactor = (double)(uLimit_)/(double)(capacity_-1);
     }
     double ratio  = boltzmann(newEnergy - oldEnergy)*boltzmann(newWeight-oldWeight)*comboPrefactor;
     //double ratio = exp(oldWeight-newWeight);
     bool   accept = random().metropolis(ratio);
     #endif
     
     if (accept) {
       incrementNAccept();
     } else {
       // Revert chosen molecule to original state
       speciesPtr_->mutator().setMoleculeState(molecule, oldStateId);
     }
     int state = mutatorPtr_->stateOccupancy(0);
     weights_[state]=weights_[state]+log(weightSize_);
     stateCount_[state]=stateCount_[state]+1;
     stepAdapt();
     return accept;
   }
 
   void WangLandauSemigrandMove::output()
   {    
     outputFile_.close();
     std::string fileName = outputFileName_; 
     std::ofstream outputFile;
     fileName += ".dat";
     system().fileMaster().openOutputFile(fileName, outputFile);
     for (int i = 0; i < capacity_; i++) {
       outputFile << i << "   " <<  weights_[i]<<std::endl;
     }
     outputFile.close();
   }
}
