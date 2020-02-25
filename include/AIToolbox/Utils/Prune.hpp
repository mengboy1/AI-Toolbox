#ifndef AI_TOOLBOX_UTILS_PRUNE_HEADER_FILE
#define AI_TOOLBOX_UTILS_PRUNE_HEADER_FILE

#include <algorithm>

#include <AIToolbox/Types.hpp>
#include <AIToolbox/Utils/Core.hpp>
#include <AIToolbox/Utils/TypeTraits.hpp>
#include <AIToolbox/Utils/Polytope.hpp>

namespace AIToolbox {
    /**
     * @brief This function finds and moves all Hyperplanes in the range that are dominated by others.
     *
     * This function performs simple comparisons between all Hyperplanes in the
     * range, and is thus much more performant than a full-fledged prune, since
     * that would need to solve multiple linear programming problems. However,
     * this function will not return the truly parsimonious set of Hyperplanes,
     * as its pruning powers are limited.
     *
     * Dominated elements will be moved at the end of the range for safe removal.
     *
     * @param begin The begin of the list that needs to be pruned.
     * @param end The end of the list that needs to be pruned.
     * @param p A projection function to call on the iterators (defaults to identity).
     *
     * @return The iterator that separates dominated elements with non-pruned.
     */
    template <typename Iterator, typename P = identity>
    Iterator extractDominated(Iterator begin, Iterator end, P p = P{}) {
        if ( std::distance(begin, end) < 2 ) return end;

        auto optEnd = begin;
        while (optEnd < end) {
            auto target = end - 1; // The one we are checking whether it is dominated.
            // Check against proven non-dominated vectors
            for (auto iter = begin; iter < optEnd; ++iter) {
                if (dominates(std::invoke(p, *iter), std::invoke(p, *target))) {
                    --end;
                    goto next;
                }
            }
            {
                // Check against others and find another non-dominated. Here
                // we go from the back so that we only swap with vectors we
                // have already checked.
                auto helper = target;
                while (helper != optEnd) {
                    --helper;
                    // If dominated, remove it and continue from there.
                    if (dominates(std::invoke(p, *helper), std::invoke(p, *target))) {
                        std::iter_swap(target, --end);
                        target = helper;
                    }
                }
                // Add vector we found in the non-dominated group
                std::iter_swap(target, optEnd);
                ++optEnd;
            }
next:;
        }
        return end;
    }

    /**
     * @brief This function finds and moves all Hyperplanes in the range that are dominated by others.
     *
     * This function is similar to extractDominated, with the additional
     * assumption that a certain set of Hyperplanes do not dominate each other.
     * This function can be useful to extract dominated Hyperplanes after new
     * ones have been added to an already pruned set. This function would then
     * skip re-checking between the already pruned Hyperplanes.
     *
     * This function assumes that the new additions are not that many with
     * respect to the already validated Hyperplanes. If that's not the case,
     * it's possible that extractDominated might be faster.
     *
     * Dominated elements will be moved at the end of the range for safe removal.
     *
     * @param begin The begin of the list that needs to be pruned.
     * @param newBegin The begin of the new Hyperplanes that need to be checked.
     * @param end The end of the list that needs to be pruned.
     * @param p A projection function to call on the iterators (defaults to identity).
     *
     * @return The iterator that separates dominated elements with non-pruned.
     */
    template <typename Iterator, typename P = identity>
    Iterator extractDominatedIncremental(Iterator begin, Iterator newBegin, Iterator end, P p = P{}) {
        while (newBegin < end) {
            auto target = end - 1;
            // For each pre-existing Hyperplane, we check whether we dominate or we are dominated.
            // - If we are dominated, we are done, as we don't belong in the
            //   good set.
            // - If we dominate, then we have to check whether we dominate
            //   anyone else, and remove all of them.
            // - If by the end no check was true, then we get placed in the
            //   good set.
            bool isDominating = false;
            for (auto iter = begin; iter < newBegin; ++iter) {
                // First check that the new entry is not dominated
                if (!isDominating && dominates(std::invoke(p, *iter), std::invoke(p, *target))) {
                    --end;
                    goto next;
                }
                // Now whether we dominate
                if (dominates(std::invoke(p, *target), std::invoke(p, *iter))) {
                    if (!isDominating) {
                        isDominating = true;

                        std::iter_swap(target, iter);
                        target = iter;
                        --end;
                    } else {
                        std::iter_swap(iter, --newBegin);
                        std::iter_swap(newBegin, --end);
                        // Backtrack iter to recheck swapped item. Note that iter
                        // here can't ever be equal to begin, so it's safe to
                        // do this.
                        --iter;
                    }
                }
            }
            if (!isDominating) {
                std::iter_swap(target, newBegin);
                ++newBegin;
            }
next:;
        }
        return end;
    }

    /**
     * @brief This class offers pruning facilities for non-parsimonious ValueFunction sets.
     *
     * This class automatically handles a series of WitnessLPs in order to
     * remove all hyperplanes which are completely dominated. It is much more
     * precise than extractDominated, but it is also a lot more expensive to
     * call.
     */
    class Pruner {
        public:
            /**
             * @brief Basic constructor.
             *
             * @param S The number of dimensions of the simplex to operate on.
             */
            Pruner(const size_t s) : S(s), lp_(S) {}

            /**
             * @brief This function prunes all non useful hyperplanes from the provided list.
             *
             * @param w The list that needs to be pruned.
             */
            template <typename It, typename P = identity>
            It operator()(It begin, It end, P p = P{});

        private:
            size_t S;

            WitnessLP lp_;
    };

    // The idea is that the input thing already has all the best vectors,
    // thus we only need to find them and discard the others.
    template <typename It, typename P>
    It Pruner::operator()(It begin, It end, P p) {
        // Remove easy ValueFunctions to avoid doing more work later.
        end = extractDominated(begin, end, p);

        const size_t size = std::distance(begin, end);
        if ( size < 2 ) return end;

        // Initialize the new best list with some easy finds, and remove them from
        // the old list.
        It bound = begin;

        bound = extractBestAtSimplexCorners(S, begin, bound, end, p);

        // Here we could do some random belief lookups..

        // If we actually have still work to do..
        if ( bound < end ) {
            // We setup the lp preparing for a max of size rows.
            lp_.reset();
            lp_.allocate(size);

            // Setup initial LP rows. Note that best can't be empty, since we have
            // at least one best for the simplex corners.
            for ( auto it = begin; it != bound; ++it )
                lp_.addOptimalRow(std::invoke(p, *it));
        }

        // For each of the remaining points now we try to find a witness
        // point with respect to the best ones. If there is, there is
        // something we need to extract to best.
        //
        // What we are going to do is to push each 'best' constraint into
        // the lp, and then push/pop the 'v' constraint every time we need
        // to try out a new one.
        //
        // That we do in the findWitnessPoint function.
        while ( bound < end ) {
            const auto witness = lp_.findWitness(std::invoke(p, *(end-1)));
            // If we get a belief point, we search for the actual vector that provides
            // the best value on the belief point, we move it into the best vector.
            if ( witness ) {
                // Advance bound with the next best
                bound = extractBestAtPoint(*witness, bound, bound, end, p);
                // Add the newly found vector to our lp.
                lp_.addOptimalRow(std::invoke(p, *(bound-1)));
            }
            // We only advance if we did not find anything. Otherwise, we may have found a
            // witness point for the current value, but since we are not guaranteed to have
            // put into best that value, it may still keep witness to other belief points!
            else
                --end;
        }

        return bound;
    }
}

#endif
