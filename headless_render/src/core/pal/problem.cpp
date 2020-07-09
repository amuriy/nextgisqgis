/*
 *   libpal - Automated Placement of Labels Library
 *
 *   Copyright (C) 2008 Maxence Laurent, MIS-TIC, HEIG-VD
 *                      University of Applied Sciences, Western Switzerland
 *                      http://www.hes-so.ch
 *
 *   Contact:
 *      maxence.laurent <at> heig-vd <dot> ch
 *    or
 *      eric.taillard <at> heig-vd <dot> ch
 *
 * This file is part of libpal.
 *
 * libpal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * libpal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with libpal.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "pal.h"
#include "palstat.h"
#include "layer.h"
#include "feature.h"
#include "geomfunction.h"
#include "labelposition.h"
#include "problem.h"
#include "util.h"
#include "priorityqueue.h"
#include "internalexception.h"
#include <cfloat>
#include <limits> //for std::numeric_limits<int>::max()

#include "qgslabelingengine.h"

using namespace pal;

inline void delete_chain( Chain *chain )
{
  if ( chain )
  {
    delete[] chain->feat;
    delete[] chain->label;
    delete chain;
  }
}

Problem::Problem( const QgsRectangle &extent )
  : mAllCandidatesIndex( extent )
  , mActiveCandidatesIndex( extent )
{

}

Problem::~Problem() = default;

void Problem::reduce()
{
  int i;
  int j;
  int k;

  int counter = 0;

  int lpid;

  bool *ok = new bool[mTotalCandidates];
  bool run = true;

  for ( i = 0; i < mTotalCandidates; i++ )
    ok[i] = false;


  double amin[2];
  double amax[2];
  LabelPosition *lp2 = nullptr;

  while ( run )
  {
    run = false;
    for ( i = 0; i < static_cast< int >( mFeatureCount ); i++ )
    {
      // ok[i] = true;
      for ( j = 0; j < mFeatNbLp[i]; j++ )  // foreach candidate
      {
        if ( !ok[mFeatStartId[i] + j] )
        {
          if ( mLabelPositions.at( mFeatStartId[i] + j )->getNumOverlaps() == 0 ) // if candidate has no overlap
          {
            run = true;
            ok[mFeatStartId[i] + j] = true;
            // 1) remove worse candidates from candidates
            // 2) update nb_overlaps
            counter += mFeatNbLp[i] - j - 1;

            for ( k = j + 1; k < mFeatNbLp[i]; k++ )
            {

              lpid = mFeatStartId[i] + k;
              ok[lpid] = true;
              lp2 = mLabelPositions[lpid ].get();

              lp2->getBoundingBox( amin, amax );

              mNbOverlap -= lp2->getNumOverlaps();
              mAllCandidatesIndex.intersects( QgsRectangle( amin[0], amin[1], amax[0], amax[1] ), [&lp2]( const LabelPosition * lp ) -> bool
              {
                if ( lp2->isInConflict( lp ) )
                {
                  const_cast< LabelPosition * >( lp )->decrementNumOverlaps();
                  lp2->decrementNumOverlaps();
                }

                return true;
              } );
              lp2->removeFromIndex( mAllCandidatesIndex );
            }

            mFeatNbLp[i] = j + 1;
            break;
          }
        }
      }
    }
  }

  this->mTotalCandidates -= counter;
  delete[] ok;
}

void ignoreLabel( const LabelPosition *lp, PriorityQueue &list, PalRtree< LabelPosition > &candidatesIndex )
{
  if ( list.isIn( lp->getId() ) )
  {
    list.remove( lp->getId() );

    double amin[2];
    double amax[2];
    lp->getBoundingBox( amin, amax );
    candidatesIndex.intersects( QgsRectangle( amin[0], amin[1], amax[0], amax[1] ), [lp, &list]( const LabelPosition * lp2 )->bool
    {
      if ( lp2->getId() != lp->getId() && list.isIn( lp2->getId() ) && lp2->isInConflict( lp ) )
      {
        list.decreaseKey( lp2->getId() );
      }
      return true;
    } );
  }
}

/* Better initial solution
 * Step one FALP (Yamamoto, Camara, Lorena 2005)
 */
void Problem::init_sol_falp()
{
  int label;

  mSol.init( mFeatureCount );

  PriorityQueue list( mTotalCandidates, mAllNblp, true );

  double amin[2];
  double amax[2];

  LabelPosition *lp = nullptr;

  for ( int i = 0; i < static_cast< int >( mFeatureCount ); i++ )
    for ( int j = 0; j < mFeatNbLp[i]; j++ )
    {
      label = mFeatStartId[i] + j;
      try
      {
        list.insert( label, mLabelPositions.at( label )->getNumOverlaps() );
      }
      catch ( pal::InternalException::Full & )
      {
        continue;
      }
    }

  while ( list.getSize() > 0 ) // O (log size)
  {
    if ( pal->isCanceled() )
    {
      return;
    }

    label = list.getBest();   // O (log size)

    lp = mLabelPositions[ label ].get();

    if ( lp->getId() != label )
    {
      //error
    }

    int probFeatId = lp->getProblemFeatureId();
    mSol.activeLabelIds[probFeatId] = label;

    for ( int i = mFeatStartId[probFeatId]; i < mFeatStartId[probFeatId] + mFeatNbLp[probFeatId]; i++ )
    {
      ignoreLabel( mLabelPositions[ i ].get(), list, mAllCandidatesIndex );
    }


    lp->getBoundingBox( amin, amax );

    std::vector< const LabelPosition * > conflictingPositions;
    mAllCandidatesIndex.intersects( QgsRectangle( amin[0], amin[1], amax[0], amax[1] ), [lp, &conflictingPositions]( const LabelPosition * lp2 ) ->bool
    {
      if ( lp->isInConflict( lp2 ) )
      {
        conflictingPositions.emplace_back( lp2 );
      }
      return true;
    } );

    for ( const LabelPosition *conflict : conflictingPositions )
    {
      ignoreLabel( conflict, list, mAllCandidatesIndex );
    }

    mActiveCandidatesIndex.insert( lp, QgsRectangle( amin[0], amin[1], amax[0], amax[1] ) );
  }

  if ( mDisplayAll )
  {
    int nbOverlap;
    int start_p;
    LabelPosition *retainedLabel = nullptr;
    int p;

    for ( std::size_t i = 0; i < mFeatureCount; i++ ) // forearch hidden feature
    {
      if ( mSol.activeLabelIds[i] == -1 )
      {
        nbOverlap = std::numeric_limits<int>::max();
        start_p = mFeatStartId[i];
        for ( p = 0; p < mFeatNbLp[i]; p++ )
        {
          lp = mLabelPositions[ start_p + p ].get();
          lp->resetNumOverlaps();

          lp->getBoundingBox( amin, amax );


          mActiveCandidatesIndex.intersects( QgsRectangle( amin[0], amin[1], amax[0], amax[1] ), [&lp]( const LabelPosition * lp2 )->bool
          {
            if ( lp->isInConflict( lp2 ) )
            {
              lp->incrementNumOverlaps();
            }
            return true;
          } );

          if ( lp->getNumOverlaps() < nbOverlap )
          {
            retainedLabel = lp;
            nbOverlap = lp->getNumOverlaps();
          }
        }
        mSol.activeLabelIds[i] = retainedLabel->getId();

        retainedLabel->insertIntoIndex( mActiveCandidatesIndex );

      }
    }
  }
}

inline Chain *Problem::chain( int seed )
{
  int lid;

  double delta;
  double delta_min;
  double delta_best = std::numeric_limits<double>::max();
  double delta_tmp;

  int next_seed;
  int retainedLabel;
  Chain *retainedChain = nullptr;

  int max_degree = pal->mEjChainDeg;

  int seedNbLp;

  QLinkedList<ElemTrans *> currentChain;
  QLinkedList<int> conflicts;

  std::vector< int > tmpsol( mSol.activeLabelIds );

  LabelPosition *lp = nullptr;

  double amin[2];
  double amax[2];

  delta = 0;
  while ( seed != -1 )
  {
    seedNbLp = mFeatNbLp[seed];
    delta_min = std::numeric_limits<double>::max();

    next_seed = -1;
    retainedLabel = -2;

    // sol[seed] is ejected
    if ( tmpsol[seed] == -1 )
      delta -= mInactiveCost[seed];
    else
      delta -= mLabelPositions.at( tmpsol[seed] )->cost();

    for ( int i = -1; i < seedNbLp; i++ )
    {
      try
      {
        // Skip active label !
        if ( !( tmpsol[seed] == -1 && i == -1 ) && i + mFeatStartId[seed] != tmpsol[seed] )
        {
          if ( i != -1 ) // new_label
          {
            lid = mFeatStartId[seed] + i;
            delta_tmp = delta;

            lp = mLabelPositions[ lid ].get();

            // evaluate conflicts graph in solution after moving seed's label

            lp->getBoundingBox( amin, amax );
            mActiveCandidatesIndex.intersects( QgsRectangle( amin[0], amin[1], amax[0], amax[1] ), [lp, &delta_tmp, &conflicts, &currentChain, this]( const LabelPosition * lp2 ) -> bool
            {
              if ( lp2->isInConflict( lp ) )
              {
                const int feat = lp2->getProblemFeatureId();

                // is there any cycles ?
                QLinkedList< ElemTrans * >::iterator cur;
                for ( cur = currentChain.begin(); cur != currentChain.end(); ++cur )
                {
                  if ( ( *cur )->feat == feat )
                  {
                    throw - 1;
                  }
                }

                if ( !conflicts.contains( feat ) )
                {
                  conflicts.append( feat );
                  delta_tmp += lp2->cost() + mInactiveCost[feat];
                }
              }
              return true;
            } );

            // no conflict -> end of chain
            if ( conflicts.isEmpty() )
            {
              if ( !retainedChain || delta + lp->cost() < delta_best )
              {
                if ( retainedChain )
                {
                  delete[] retainedChain->label;
                  delete[] retainedChain->feat;
                }
                else
                {
                  retainedChain = new Chain();
                }

                delta_best = delta + lp->cost();

                retainedChain->degree = currentChain.size() + 1;
                retainedChain->feat  = new int[retainedChain->degree];
                retainedChain->label = new int[retainedChain->degree];
                QLinkedList<ElemTrans *>::iterator current = currentChain.begin();
                ElemTrans *move = nullptr;
                int j = 0;
                while ( current != currentChain.end() )
                {
                  move = *current;
                  retainedChain->feat[j]  = move->feat;
                  retainedChain->label[j] = move->new_label;
                  ++current;
                  ++j;
                }
                retainedChain->feat[j] = seed;
                retainedChain->label[j] = lid;
                retainedChain->delta = delta + lp->cost();
              }
            }

            // another feature can be ejected
            else if ( conflicts.size() == 1 )
            {
              if ( delta_tmp < delta_min )
              {
                delta_min = delta_tmp;
                retainedLabel = lid;
                next_seed = conflicts.takeFirst();
              }
              else
              {
                conflicts.takeFirst();
              }
            }
            else
            {

              // A lot of conflict : make them inactive and store chain
              Chain *newChain = new Chain();
              newChain->degree = currentChain.size() + 1 + conflicts.size();
              newChain->feat  = new int[newChain->degree];
              newChain->label = new int[newChain->degree];
              QLinkedList<ElemTrans *>::iterator current = currentChain.begin();
              ElemTrans *move = nullptr;
              int j = 0;

              while ( current != currentChain.end() )
              {
                move = *current;
                newChain->feat[j]  = move->feat;
                newChain->label[j] = move->new_label;
                ++current;
                ++j;
              }

              // add the current candidates into the chain
              newChain->feat[j] = seed;
              newChain->label[j] = lid;
              newChain->delta = delta + mLabelPositions.at( newChain->label[j] )->cost();
              j++;

              // hide all conflictual candidates
              while ( !conflicts.isEmpty() )
              {
                int ftid = conflicts.takeFirst();
                newChain->feat[j] = ftid;
                newChain->label[j] = -1;
                newChain->delta += mInactiveCost[ftid];
                j++;
              }

              if ( newChain->delta < delta_best )
              {
                if ( retainedChain )
                  delete_chain( retainedChain );

                delta_best = newChain->delta;
                retainedChain = newChain;
              }
              else
              {
                delete_chain( newChain );
              }
            }

          }
          else   // Current label == -1   end of chain ...
          {
            if ( !retainedChain || delta + mInactiveCost[seed] < delta_best )
            {
              if ( retainedChain )
              {
                delete[] retainedChain->label;
                delete[] retainedChain->feat;
              }
              else
                retainedChain = new Chain();

              delta_best = delta + mInactiveCost[seed];

              retainedChain->degree = currentChain.size() + 1;
              retainedChain->feat  = new int[retainedChain->degree];
              retainedChain->label = new int[retainedChain->degree];
              QLinkedList<ElemTrans *>::iterator current = currentChain.begin();
              ElemTrans *move = nullptr;
              int j = 0;
              while ( current != currentChain.end() )
              {
                move = *current;
                retainedChain->feat[j]  = move->feat;
                retainedChain->label[j] = move->new_label;
                ++current;
                ++j;
              }
              retainedChain->feat[j] = seed;
              retainedChain->label[j] = -1;
              retainedChain->delta = delta + mInactiveCost[seed];
            }
          }
        }
      }
      catch ( int )
      {
        conflicts.clear();
      }
    } // end foreach labelposition

    if ( next_seed == -1 )
    {
      seed = -1;
    }
    else if ( currentChain.size() > max_degree )
    {
      // Max degree reached
      seed = -1;
    }
    else
    {
      ElemTrans *et = new ElemTrans();
      et->feat  = seed;
      et->old_label = tmpsol[seed];
      et->new_label = retainedLabel;
      currentChain.append( et );

      if ( et->old_label != -1 )
      {
        mLabelPositions.at( et->old_label )->removeFromIndex( mActiveCandidatesIndex );
      }

      if ( et->new_label != -1 )
      {
        mLabelPositions.at( et->new_label )->insertIntoIndex( mActiveCandidatesIndex );
      }


      tmpsol[seed] = retainedLabel;
      // cppcheck-suppress invalidFunctionArg
      delta += mLabelPositions.at( retainedLabel )->cost();
      seed = next_seed;
    }
  }

  while ( !currentChain.isEmpty() )
  {
    std::unique_ptr< ElemTrans > et( currentChain.takeFirst() );

    if ( et->new_label != -1 )
    {
      mLabelPositions.at( et->new_label )->removeFromIndex( mActiveCandidatesIndex );
    }

    if ( et->old_label != -1 )
    {
      mLabelPositions.at( et->old_label )->insertIntoIndex( mActiveCandidatesIndex );
    }
  }

  return retainedChain;
}


void Problem::chain_search()
{
  if ( mFeatureCount == 0 )
    return;

  int i;
  int seed;
  bool *ok = new bool[mFeatureCount];
  int fid;
  int lid;
  int popit = 0;

  Chain *retainedChain = nullptr;

  std::fill( ok, ok + mFeatureCount, false );

  //initialization();
  init_sol_falp();

  //check_solution();
  solution_cost();

  int iter = 0;

  double amin[2];
  double amax[2];

  while ( true )
  {

    //check_solution();

    for ( seed = ( iter + 1 ) % mFeatureCount;
          ok[seed] && seed != iter;
          seed = ( seed + 1 ) % mFeatureCount )
      ;

    // All seeds are OK
    if ( seed == iter )
    {
      break;
    }

    iter = ( iter + 1 ) % mFeatureCount;
    retainedChain = chain( seed );

    if ( retainedChain && retainedChain->delta < - EPSILON )
    {
      // apply modification
      for ( i = 0; i < retainedChain->degree; i++ )
      {
        fid = retainedChain->feat[i];
        lid = retainedChain->label[i];

        if ( mSol.activeLabelIds[fid] >= 0 )
        {
          LabelPosition *old = mLabelPositions[ mSol.activeLabelIds[fid] ].get();
          old->removeFromIndex( mActiveCandidatesIndex );
          old->getBoundingBox( amin, amax );
          mAllCandidatesIndex.intersects( QgsRectangle( amin[0], amin[1], amax[0], amax[1] ), [&ok, old]( const LabelPosition * lp ) ->bool
          {
            if ( old->isInConflict( lp ) )
            {
              ok[lp->getProblemFeatureId()] = false;
            }

            return true;
          } );
        }

        mSol.activeLabelIds[fid] = lid;

        if ( mSol.activeLabelIds[fid] >= 0 )
        {
          mLabelPositions.at( lid )->insertIntoIndex( mActiveCandidatesIndex );
        }

        ok[fid] = false;
      }
      mSol.totalCost += retainedChain->delta;
    }
    else
    {
      // no chain or the one is not god enough
      ok[seed] = true;
    }

    delete_chain( retainedChain );
    popit++;
  }

  solution_cost();
  delete[] ok;
}

QList<LabelPosition *> Problem::getSolution( bool returnInactive, QList<LabelPosition *> *unlabeled )
{
  QList<LabelPosition *> solList;

  for ( std::size_t i = 0; i < mFeatureCount; i++ )
  {
    if ( mSol.activeLabelIds[i] != -1 )
    {
      solList.push_back( mLabelPositions[ mSol.activeLabelIds[i] ].get() ); // active labels
    }
    else if ( returnInactive
              || ( mFeatStartId[i] < static_cast< int >( mLabelPositions.size() ) &&
                   ( mLabelPositions.at( mFeatStartId[i] )->getFeaturePart()->layer()->displayAll()
                     || mLabelPositions.at( mFeatStartId[i] )->getFeaturePart()->alwaysShow() ) ) )
    {
      solList.push_back( mLabelPositions[ mFeatStartId[i] ].get() ); // unplaced label
    }
    else if ( unlabeled )
    {
      const int startPos = mFeatStartId[i];
      // need to be careful here -- if the next feature's start id is the same as this one, then this feature had no candidates!
      if ( startPos < static_cast< int >( mLabelPositions.size() ) && ( i == mFeatureCount - 1 || startPos != mFeatStartId[i + 1] ) )
        unlabeled->push_back( mLabelPositions[ startPos ].get() );
    }
  }

  // unlabeled features also include those with no candidates
  if ( unlabeled )
  {
    for ( const std::unique_ptr< LabelPosition > &position : mPositionsWithNoCandidates )
      unlabeled->append( position.get() );
  }

  return solList;
}

void Problem::solution_cost()
{
  mSol.totalCost = 0.0;

  LabelPosition *lp = nullptr;

  double amin[2];
  double amax[2];

  for ( std::size_t i = 0; i < mFeatureCount; i++ )
  {
    if ( mSol.activeLabelIds[i] == -1 )
    {
      mSol.totalCost += mInactiveCost[i];
    }
    else
    {
      lp = mLabelPositions[ mSol.activeLabelIds[i] ].get();

      lp->getBoundingBox( amin, amax );
      mActiveCandidatesIndex.intersects( QgsRectangle( amin[0], amin[1], amax[0], amax[1] ), [&lp, this]( const LabelPosition * lp2 )->bool
      {
        if ( lp->isInConflict( lp2 ) )
        {
          mSol.totalCost += mInactiveCost[lp2->getProblemFeatureId()] + lp2->cost();
        }

        return true;
      } );

      mSol.totalCost += lp->cost();
    }
  }
}
