#ifndef MICELLE_MC_MOVE_FACTORY_H
#define MICELLE_MC_MOVE_FACTORY_H

#include <mcMd/mcMoves/McMoveFactory.h>

namespace McMd
{

   using namespace Util;

   class McSimulation;
   class McSystem;

   /**
   * Custom McMoveFactory.
   */
   class MicelleMcMoveFactory : public McMoveFactory
   {

   public:

      /**
      * Constructor.
      *
      * \param simulation parent simulation
      * \param system     parent system
      */
      MicelleMcMoveFactory(McSimulation& simulation, McSystem& system);

      /** 
      * Return pointer to a new McMove object.
      *
      * \param  className name of a subclass of McMove.
      * \return base class pointer to a new instance of className.
      */
      virtual McMove* factory(const std::string& className) const;

   };

}
#endif
