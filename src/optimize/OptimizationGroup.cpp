#include <tsn/optimize/OptimizationGroup.h>
#include <tsn/compiler/CodeHolder.h>
#include <utils/Array.hpp>

namespace tsn {
    namespace optimize {
        OptimizationGroup::OptimizationGroup(Context* ctx) : IOptimizationStep(ctx) {
        }

        OptimizationGroup::~OptimizationGroup() {
            m_steps.each([](IOptimizationStep* step) {
                delete step;
            });

            m_steps.clear();
        }

        void OptimizationGroup::setShouldRepeat(bool doRepeat) {
            m_doRepeat = true;
        }

        bool OptimizationGroup::willRepeat() const {
            return m_doRepeat;
        }

        void OptimizationGroup::addStep(IOptimizationStep* step) {
            m_steps.push(step);
            step->setGroup(this);
        }

        bool OptimizationGroup::execute(compiler::CodeHolder* code, Pipeline* pipeline) {
            m_doRepeat = false;

            m_steps.each([code, pipeline](IOptimizationStep* step) {
                for (auto& b : code->cfg.blocks) {
                    while (step->execute(code, &b, pipeline));
                }
                
                while (step->execute(code, pipeline));
            });

            return m_doRepeat;
        }
    };
};