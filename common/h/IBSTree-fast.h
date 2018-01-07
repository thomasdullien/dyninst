/*
 * See the dyninst/COPYRIGHT file for copyright information.
 *
 * We provide the Paradyn Tools (below described as "Paradyn")
 * on an AS IS basis, and do not warrant its validity or performance.
 * We reserve the right to update, modify, or discontinue this
 * software at any time.  We shall have no obligation to supply such
 * updates or modifications or any other form of support to you.
 *
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#if !defined(IBSTREE_FAST_H)
#define IBSTREE_FAST_H
#include "IBSTree.h"
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/mpl/insert_range.hpp>
#include <boost/mpl/inherit_linearly.hpp>
#include <boost/mpl/inherit.hpp>
#include <iostream>

#include <boost/thread/locks.hpp>
#include <boost/thread/lockable_adapter.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <boost/thread/shared_lock_guard.hpp>

#include <race-detector-annotations.h>

namespace Dyninst
{

    template <typename ITYPE >
    class IBSTree_fast : public boost::upgrade_lockable_adapter<boost::upgrade_mutex>
    {
    public:
        typedef typename ITYPE::type interval_type;

        IBSTree<ITYPE> overlapping_intervals;
        typedef boost::multi_index_container<ITYPE*,
                boost::multi_index::indexed_by<
                        boost::multi_index::ordered_unique<
                                boost::multi_index::const_mem_fun<ITYPE, interval_type, &ITYPE::high> >
                > > interval_set;

        //typedef std::set<ITYPE*, order_by_lower<ITYPE> > interval_set;
        interval_set unique_intervals;

        IBSTree_fast()
        {
        }
        ~IBSTree_fast()
        {
            //std::cerr << "Fast interval tree had " << unique_intervals.size() << " unique intervals and " << overlapping_intervals.size() << " overlapping" << std::endl;
        }
        int size() const
        {
            boost::shared_lock_guard<const IBSTree_fast<ITYPE> > g(*this);
            return overlapping_intervals.size() + unique_intervals.size();
        }
        bool empty() const
        {
            boost::shared_lock_guard<const IBSTree_fast<ITYPE> > g(*this);
            return unique_intervals.empty() && overlapping_intervals.empty();
        }
        void insert(ITYPE*);
        void remove(ITYPE*);
        int find(interval_type, std::set<ITYPE*> &) const;
        int find(ITYPE* I, std::set<ITYPE*>&) const;
        void successor(interval_type X, std::set<ITYPE*>& ) const;
        ITYPE* successor(interval_type X) const;
        void clear();
        friend std::ostream& operator<<(std::ostream& stream, const IBSTree_fast<ITYPE>& tree)
        {
            boost::shared_lock_guard<const IBSTree_fast<ITYPE> > g(tree);
            std::copy(tree.unique_intervals.begin(), tree.unique_intervals.end(),
                      std::ostream_iterator<typename Dyninst::IBSTree_fast<ITYPE>::interval_set::value_type>(stream, "\n"));
            stream << tree.overlapping_intervals;
            return stream;
        }

    };

    template <class ITYPE>
    void IBSTree_fast<ITYPE>::insert(ITYPE* entry)
    {
      // suppress race reports from shared lock guard initialization inside boost
      race_detector_fake_lock_acquire(race_detector_fake_lock(*this));
      {
        boost::lock_guard<IBSTree_fast<ITYPE> > u(*this);
        // find in overlapping first
        std::set<ITYPE*> dummy;
        if(overlapping_intervals.find(entry, dummy))
        {
            overlapping_intervals.insert(entry);
        } else { 
	  typename interval_set::iterator lower =
	    unique_intervals.upper_bound(entry->low());
	  // lower.high first >= entry.low
	  if(lower != unique_intervals.end() && (**lower == *entry)) return;
	  typename interval_set::iterator upper = lower;
	  while(upper != unique_intervals.end() &&
		(*upper)->low() <= entry->high())
	    {
	      overlapping_intervals.insert(*upper);
	      ++upper;
	    }
	  if(upper != lower)
	    {
	      unique_intervals.erase(lower, upper);
	      overlapping_intervals.insert(entry);
	    }
	  else
	    {
	      unique_intervals.insert(entry);
	    }
	}
      }
      race_detector_fake_lock_release(race_detector_fake_lock(*this));
    }
    template <class ITYPE>
    void IBSTree_fast<ITYPE>::remove(ITYPE* entry)
    {
      // race_detector_fake_lock_acquire(race_detector_fake_lock(*this));
      {
        boost::lock_guard<IBSTree_fast<ITYPE> > u(*this);
        overlapping_intervals.remove(entry);
        typename interval_set::iterator found = unique_intervals.find(entry->high());
        if(found != unique_intervals.end() && *found == entry) unique_intervals.erase(found);
      }
      // race_detector_fake_lock_release(race_detector_fake_lock(*this));
    }
    template<class ITYPE>
    int IBSTree_fast<ITYPE>::find(interval_type X, std::set<ITYPE*> &results) const
    {
      int count = 0;
      // suppress race reports from shared lock guard initialization inside boost
      race_detector_fake_lock_acquire(race_detector_fake_lock(*this));
      do 
      {
        boost::shared_lock_guard<const IBSTree_fast<ITYPE> > g(*this);
        int num_old_results = results.size();

        int num_overlapping = overlapping_intervals.find(X, results);
        if(num_overlapping > 0) { count = num_overlapping; break; }

        typename interval_set::const_iterator found_unique = unique_intervals.upper_bound(X);
        if(found_unique != unique_intervals.end())
        {
	  if((*found_unique)->low() > X) { count = 0; break; }
            results.insert(*found_unique);
        }
        count = results.size() - num_old_results;
      } while (0);
      race_detector_fake_lock_release(race_detector_fake_lock(*this));
      return count;
    }
    template <typename ITYPE>
    int IBSTree_fast<ITYPE>::find(ITYPE* I, std::set<ITYPE*>&results) const
    {
        boost::shared_lock_guard<const IBSTree_fast<ITYPE> > g(*this);
        int num_old_results = results.size();
        int num_overlapping = overlapping_intervals.find(I, results);
        if(num_overlapping) return num_overlapping;
        typename interval_set::const_iterator lb = unique_intervals.upper_bound(I->low());
        typename interval_set::iterator  ub = lb;
        while(ub != unique_intervals.end() && (*ub)->low() < I->high())
        {
            results.insert(*ub);
            ++ub;
        }
        return results.size() - num_old_results;
    }
    template<typename ITYPE>
    void IBSTree_fast<ITYPE>::successor(interval_type X, std::set<ITYPE*>& results) const
    {
        boost::shared_lock_guard<const IBSTree_fast<ITYPE> > g(*this);
        ITYPE* overlapping_ub = overlapping_intervals.successor(X);

        typename interval_set::const_iterator unique_ub = unique_intervals.upper_bound(X);
        if(overlapping_ub && (unique_ub != unique_intervals.end()))
        {
            results.insert(overlapping_ub->low() < (*unique_ub)->low() ? overlapping_ub : *unique_ub);
        }
        else if(overlapping_ub)
        {
            results.insert(overlapping_ub);
        }
        else if(unique_ub != unique_intervals.end())
        {
            results.insert(*unique_ub);
        }
    }
    template <typename ITYPE>
    ITYPE* IBSTree_fast<ITYPE>::successor(interval_type X) const
    {
        boost::shared_lock_guard<const IBSTree_fast<ITYPE> > g(*this);
        std::set<ITYPE*> tmp;
        successor(X, tmp);
        assert(tmp.size() <= 1);
        if(tmp.empty()) return NULL;
        return *tmp.begin();

    }
    template <typename ITYPE>
    void IBSTree_fast<ITYPE>::clear()
    {
      // race_detector_fake_lock_acquire(race_detector_fake_lock(*this));
      {
        boost::lock_guard<IBSTree_fast<ITYPE> > u(*this);
        overlapping_intervals.clear();
        unique_intervals.clear();
      }
      // race_detector_fake_lock_release(race_detector_fake_lock(*this));
    }

}


#endif
