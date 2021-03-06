/*
* Simpatico - Simulation Package for Polymeric and Molecular Liquids
*
* Copyright 2010 - 2014, The Regents of the University of Minnesota
* Distributed under the terms of the GNU General Public License.
*/

#include "UmbrellaSamplingSemigrandMove.h"
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
   UmbrellaSamplingSemigrandMove::UmbrellaSamplingSemigrandMove(McSystem& system) : 
      SystemMove(system),
      speciesId_(-1),
      mutatorPtr_(0),
      outputFileName_(),
      initialWeightFileName_("0"),
      stepCount_(0)
   {  setClassName("MuellerMove"); } 
   
   /* 
   * Read parameter speciesId.
   */
   void UmbrellaSamplingSemigrandMove::readParameters(std::istream& in) 
   {
      // Read parameters
      readProbability(in);
      read<int>(in, "speciesId", speciesId_);
      // Cast the Species to LinearSG
      speciesPtr_ = dynamic_cast<LinearSG*>(&(simulation().species(speciesId_)));
      if (!speciesPtr_) {
         UTIL_THROW("Error: Species must be LinearSG");
      }
      capacity_ = speciesPtr_->capacity()+1; 
      mutatorPtr_ = &speciesPtr_->mutator();
      weights_.allocate(capacity_);
      read<int>(in, "upperLimit", uLimit_);
      read<int>(in, "lowerLimit", lLimit_);
      read<std::string>(in, "outputFileName",outputFileName_);
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
   }
   /*
   * Load state from an archive.
   */
   void UmbrellaSamplingSemigrandMove::loadParameters(Serializable::IArchive& ar)
   {  
      McMove::loadParameters(ar);
      loadParameter<int>(ar, "speciesId", speciesId_);
      // Cast the Species to LinearSG
      speciesPtr_ = dynamic_cast<LinearSG*>(&(simulation().species(speciesId_)));
      if (!speciesPtr_) {
         UTIL_THROW("Species is not a LinearSG");
      }
      loadParameter<int>(ar, "UpperLimit", uLimit_);
      loadParameter<int>(ar, "LowerLimit", lLimit_);
      mutatorPtr_ = &speciesPtr_->mutator();
      ar & weights_;
   }
   

   /*
   * Save state to an archive.
   */
   void UmbrellaSamplingSemigrandMove::save(Serializable::OArchive& ar)
   {
      McMove::save(ar);
      ar & speciesId_;  
      ar & uLimit_;
      ar & lLimit_;
      ar & weights_;
   }
     

   Molecule& UmbrellaSamplingSemigrandMove::randomSGMolecule(int speciesId, int nSubType, int flipType, bool atBoundary)
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
   bool UmbrellaSamplingSemigrandMove::move() 
   { 
      incrementNAttempt();
      double comboPrefactor = 1.0;
      int flipType = -1.0;
      int flipTypeCapacity;
      bool atBound=false;
      bool wasAtUpperBound=false;
      bool wasAtLowerBound=false;
      // Special semigrand selector
      int oldStateCount = mutatorPtr_->stateOccupancy(0);
      int oldState = mutatorPtr_->stateOccupancy(0);
      if  (oldStateCount == uLimit_) {
        atBound=true;
        wasAtUpperBound=true;
        flipType = 0;
        flipTypeCapacity =  mutatorPtr_->stateOccupancy(0);
      }
      if (oldStateCount == lLimit_) {
        atBound = true;
        wasAtLowerBound=true;
        flipType = 1;
        flipTypeCapacity =  mutatorPtr_->stateOccupancy(1); 
      }
      Molecule& molecule = randomSGMolecule(speciesId_, flipTypeCapacity, flipType, atBound);
      #ifndef INTER_NOPAIR
      // Calculate pair energy for the chosen molecule
      double oldEnergy = system().pairPotential().moleculeEnergy(molecule);
      #endif
      // Toggle state of the molecule
      int oldStateId = speciesPtr_->mutator().moleculeStateId(molecule);
      int newStateId = (oldStateId == 0) ? 1 : 0;
      speciesPtr_->mutator().setMoleculeState(molecule, newStateId);
      #ifdef INTER_NOPAIR 
      bool   accept = true;
      #else //ifndef INTER_NOPAIR
      // Recalculate pair energy for the molecule
      double newEnergy = system().pairPotential().moleculeEnergy(molecule);
      // Decide whether to accept or reject
      int    newState = mutatorPtr_->stateOccupancy(0);
      // If the system was at either the upper or lower bound calculate the combinatorial prefactor necessary for detailed balance
      if (wasAtLowerBound) {
        comboPrefactor = (double)(capacity_-1-lLimit_)/(double)(capacity_-1);
      }
      if (wasAtUpperBound) {
        comboPrefactor = (double)(uLimit_)/(double)(capacity_-1);
      }
      // Different move if the move is with in the desired range or not
      //int    oldState = newState - stateChange;
      double oldWeight = weights_[oldState];
      double newWeight = weights_[newState];
      double oldWeightSG = speciesPtr_->mutator().stateWeight(oldStateId);
      double newWeightSG = speciesPtr_->mutator().stateWeight(newStateId);
      double ratio  = boltzmann(newEnergy - oldEnergy)*boltzmann(newWeight-oldWeight)*newWeightSG/oldWeightSG*comboPrefactor;
      bool   accept = random().metropolis(ratio);
      #endif
      if (accept) {
        incrementNAccept();
      } else {
      // Revert chosen molecule to original state
        speciesPtr_->mutator().setMoleculeState(molecule, oldStateId);
      }
      return accept;
   }
 
   void UmbrellaSamplingSemigrandMove::output()
   {    outputFile_.close();
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
