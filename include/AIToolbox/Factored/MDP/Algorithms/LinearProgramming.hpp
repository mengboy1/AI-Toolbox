#ifndef AI_TOOLBOX_FACTORED_MDP_LINEAR_PROGRAMMING_HEADER_FILE
#define AI_TOOLBOX_FACTORED_MDP_LINEAR_PROGRAMMING_HEADER_FILE

#include <AIToolbox/Types.hpp>
#include <AIToolbox/Factored/Utils/BayesianNetwork.hpp>
#include <AIToolbox/Factored/Utils/FactoredMatrix.hpp>
#include <AIToolbox/Factored/MDP/CooperativeModel.hpp>
#include <AIToolbox/Factored/Utils/FactorGraph.hpp>

namespace AIToolbox { class LP; }

namespace AIToolbox::Factored::MDP {
    /**
     * @brief This class solves a factored MDP with Linear Programming.
     */
    class LinearProgramming {
        public:
            std::tuple<Vector, QFunction> operator()(const CooperativeModel & m, const FactoredVector & h) const;

        private:
            using Rule = std::pair<PartialValues, size_t>;
            using Rules = std::vector<Rule>;
            using Graph = FactorGraph<Rules>;

            std::optional<Vector> solveLP(const CooperativeModel & m, const Factored2DMatrix & g, const FactoredVector & h) const;
            void removeState(const Factors & F, Graph & graph, size_t s, LP & lp, std::vector<size_t> & finalFactors) const;
    };

}

#endif
