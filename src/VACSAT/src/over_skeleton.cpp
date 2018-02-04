//
// Created by esteffin on 12/5/17.
//

#include "over_skeleton.h"
#include "prelude.h"
#include "SMT_BMC_Struct.h"
#include "SMTSolvers/yices.h"
#include "SMTSolvers/Z3.h"
#include "SMTSolvers/mathsat.h"
#include "SMTSolvers/boolector.h"

namespace SMT {

    class simple_role_choicer : public role_choicer {

        const std::shared_ptr<arbac_policy> &policy;

    public:
        explicit simple_role_choicer(const std::shared_ptr<arbac_policy> &_policy) :
                policy(_policy) { }

        Choice classify(atomp r) const override {
//            if (r->name == "r1" || r->name == "r2") {
//                return REQUIRED;
//            }
            return FREE;
        }

        int get_required_count() const override {
            int count = 0;
            for (auto &&atom :policy->atoms()) {
                if (classify(atom) == REQUIRED) {
                    count++;
                }
            }
            return count;
        }

    };

    template <typename TVar, typename TExpr>
    class learning_overapprox {
    private:
        typedef generic_variable<TVar, TExpr> variable;

        class b_solver_state;

        typedef proof_node<b_solver_state> node;
        typedef std::shared_ptr<proof_node<b_solver_state>> tree;

        class b_solver_state {
        private:

            void init(SMTFactory<TVar, TExpr>* solver, const tree &node) {
                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string var_name = "var_" + node_id + "_" + atom->name;
                    vars[atom->role_array_index] = variable(var_name, 0, 1, solver, BOOLEAN);
                }

                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string updated_in_subrun_name = "updated_in_subrun_" + node_id + "_" + atom->name;
                    updated_in_subrun[atom->role_array_index] = variable(updated_in_subrun_name, 0, 1, solver,
                                                                         BOOLEAN);
                }

                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string blocked_name = "blocked_" + node_id + "_" + atom->name;
                    blocked[atom->role_array_index] = variable(blocked_name, 0, 1, solver, BOOLEAN);
                }


                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string blocked_by_children_name = "blocked_by_children_" + node_id + "_" + atom->name;
                    blocked_by_children[atom->role_array_index] = variable(blocked_by_children_name, 0, 1, solver, BOOLEAN);
                }
                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string missing_priority_name = "missing_priority_" + node_id + "_" + atom->name;
                    missing_priority[atom->role_array_index] = variable(missing_priority_name, 0, 1, solver, BOOLEAN);
                }
                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string priority_name = "priority_" + node_id + "_" + atom->name;
                    priority[atom->role_array_index] = variable(priority_name, 0, 1, solver, BOOLEAN);
                }
                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string second_priority_name = "second_priority_" + node_id + "_" + atom->name;
                    second_priority[atom->role_array_index] = variable(second_priority_name, 0, 1, solver, BOOLEAN);
                }
                for (auto &&atom : node->node_infos.all_atoms) {
                    std::string priority_not_blocked_name = "priority_not_blocked_" + node_id + "_" + atom->name;
                    priority_not_blocked[atom->role_array_index] = variable(priority_not_blocked_name, 0, 1, solver, BOOLEAN);
                }

                //TODO: can be decreased only to actual atom count, but everything should be updated accordingly
                var_id = variable("var_id_" + node_id, 0, bits_count(node->node_infos.policy_atoms_count), solver,
                                  BIT_VECTOR);
                rule_id = variable("rule_id_" + node_id, 0, bits_count((uint) node->node_infos.rules_c.size()), solver,
                                   BIT_VECTOR);
                skip = variable("skip_" + node_id, 0, 1, solver, BOOLEAN);
                guard = variable("guard_" + node_id, 0, 1, solver, BOOLEAN);
                refineable = variable("refineable_" + node_id, 0, 1, solver, BOOLEAN);
                increase_budget = variable("increase_budget_" + node_id, 0, 1, solver, BOOLEAN);
            }

        public:
            std::string &node_id;
            std::vector<variable> vars;
            std::vector<variable> updated_in_subrun;
            std::vector<variable> blocked;
            std::vector<variable> blocked_by_children;
            std::vector<variable> missing_priority;
            std::vector<variable> priority;
            std::vector<variable> second_priority;
            std::vector<variable> priority_not_blocked;
            variable var_id;
            variable rule_id;
            variable skip;
            variable guard;
            variable refineable;
            variable increase_budget;

            b_solver_state() = delete;

            b_solver_state(tree &node,
//                           const std::shared_ptr<arbac_policy>& policy,
                           SMTFactory<TVar, TExpr>* solver) :
                    node_id(node->uid),
                    vars(std::vector<variable>((uint) node->node_infos.policy_atoms_count)),
                    updated_in_subrun(std::vector<variable>((uint) node->node_infos.policy_atoms_count)),
                    blocked(std::vector<variable>((uint) node->node_infos.policy_atoms_count)),
                    blocked_by_children(std::vector<variable>((uint) node->node_infos.policy_atoms_count)),
                    missing_priority(std::vector<variable>((uint) node->node_infos.policy_atoms_count)),
                    priority(std::vector<variable>((uint) node->node_infos.policy_atoms_count)),
                    second_priority(std::vector<variable>((uint) node->node_infos.policy_atoms_count)),
                    priority_not_blocked(std::vector<variable>((uint) node->node_infos.policy_atoms_count)) {
                init(solver, node);
//                set_guards();
            }
        };

        enum over_analysis_result {
            SAFE,
            UNSAFE,
            UNSAFE_REFINEABLE
        };

        atomp target_atom = nullptr;
        atomp fake_atom = nullptr;

        class tree_to_SMT {
        private:

            const std::shared_ptr<arbac_policy> policy;
            std::shared_ptr<SMTFactory<TVar, TExpr>> solver;
            std::list<TExpr> assertions;

            variable tmp_bool;
            TExpr zero;
            TExpr one;

            // ASSERTIONS RELATED FUNCTIONS
            inline void strict_emit_assignment(const variable &var, const TVar &value) {
                TExpr ass = solver->createEqExpr(var.get_solver_var(), value);
                solver->assertLater(ass);
            }

            inline void emit_assignment(const tree& node, const variable &var, const TVar &value) {
                TExpr ass = solver->createEqExpr(var.get_solver_var(), value);
                //NOTICE: Do NOT put XOR, IMPLICATION or OR are OK, but NO XOR for the god sake! Otherwise the aserted statement MUST be false!
                TExpr guarded_ass = solver->createImplExpr(node->solver_state->guard.get_solver_var(),
                                                           ass);
                solver->assertLater(guarded_ass);

            }

            inline void emit_assumption(const tree& node, const TExpr &value) {
                //NOTICE: Do NOT put XOR, IMPLICATION or OR are OK, but NO XOR for the god sake! Otherwise the aserted statement MUST be false!
                TExpr guarded_ass = solver->createImplExpr(node->solver_state->guard.get_solver_var(),
                                                           value);
                solver->assertLater(guarded_ass);
            }

            inline void emit_comment(const std::string &comment) {
                //Working automatically and only in Z3
                if (std::is_same<z3::expr, TExpr>::value) {
                    solver->assertNow(solver->createBoolVar(comment));
                    log->debug("Emitting comment: " + comment);
                }
            }

            // STATE AND MASKS RELATED FUNCTIONS
            void set_empty_node_state(tree &node) {
                assert(node->solver_state == nullptr);
                b_solver_state node_state = b_solver_state(node, solver.get());
                node->solver_state = std::make_shared<b_solver_state>(node_state);
            }

            void set_zero(tree& node, std::vector<variable> &vars) {
                for (auto &&atom : node->node_infos.all_atoms) {

//                    log->warn("{}", var.full_name);
                        emit_assignment(node, vars[atom->role_array_index], zero);
//                        strict_emit_assignment(vars[atom->role_array_index], zero);
                }
            }

            void copy_mask(tree &node, std::vector<variable> &source, std::vector<variable> &dest) {
                if (source.size() != dest.size()) {
                    throw unexpected_error("Cannot copy from container of different sizes", __FILE__, __LINE__, __FUNCTION__, __PRETTY_FUNCTION__);
                }
                for (auto &&atom : node->node_infos.all_atoms) {
                    int i = atom->role_array_index;
                    variable s = source[i];
                    variable d = dest[i];
                    TExpr source_val = s.get_solver_var();
                    emit_assignment(node, d, source_val);
                }
            }

            /*
             * This function copies the values from source to dest if the condition holds,
             * Otherwise it takes the value from dest
             * */
            void cond_save_mask(tree &node,
                                std::vector<variable> &source,
                                std::vector<variable> &dest,
                                TExpr condition) {
                if (source.size() != dest.size()) {
                    throw unexpected_error("Cannot sync two container of different sizes", __FILE__, __LINE__, __FUNCTION__, __PRETTY_FUNCTION__);
                }
                //TODO: check if atoms_a is enough
                for (auto &&atom : node->node_infos.all_atoms) {
                    int i = atom->role_array_index;
                    variable s = source[i];
                    variable old_d = dest[i];
                    variable new_d = old_d.createFrom();

                    TExpr value = solver->createCondExpr(condition,
                                                         s.get_solver_var(),
                                                         old_d.get_solver_var());
                    emit_assignment(node, new_d, value);
                    dest[i] = new_d;
                }
            }

            /*
             * This function set variables in dest to the OR of source1 and source2 if the condition holds,
             * Otherwise it takes the value from dest
             * */
            void cond_save_ored_mask(tree &node,
                                     std::vector<variable> &source1,
                                     std::vector<variable> &source2,
                                     std::vector<variable> &dest,
                                     TExpr condition) {
                if (source1.size() != source2.size() || source1.size() != dest.size()) {
                    throw unexpected_error("Cannot sync two container of different sizes", __FILE__, __LINE__, __FUNCTION__, __PRETTY_FUNCTION__);
                }
                //TODO: check if atoms_a is enough
                for (auto &&atom : node->node_infos.all_atoms) {
                    int i = atom->role_array_index;
                    variable s1 = source1[i];
                    variable s2 = source2[i];
                    variable old_d = dest[i];
                    variable new_d = old_d.createFrom();

                    TExpr value = solver->createCondExpr(condition,
                                                         solver->createOrExpr(s1.get_solver_var(), s2.get_solver_var()),
                                                         old_d.get_solver_var());
                    emit_assignment(node, new_d, value);
                    dest[i] = new_d;
                }
            }

            // NONDET ASSIGNMENT RELATED FUNCTIONS
            TExpr get_variable_invariant_from_node(tree &node, const Atomp &var) {
                variable var_value = node->solver_state->vars[var->role_array_index];
                std::set<bool> values;
                for (auto &&rule :node->node_infos.rules_a) {
                    if (rule->target == var) {
                        values.insert(rule->is_ca);
                    }
                }

                TExpr value_valid = solver->createFalse();
                for (auto &&value : values) {
                    TExpr assigned_value = value ? one : zero;
                    value_valid = solver->createOrExpr(value_valid,
                                                       solver->createEqExpr(var_value.get_solver_var(),
                                                                            assigned_value));
                }
                return value_valid;
            }

            variable update_var_a(tree &node, const Atomp &var) {
                //GUARD FOR NONDET UPDATE
                tmp_bool = tmp_bool.createFrom();
                TExpr update_guard =
                        solver->createAndExpr(solver->createNotExpr(
                                node->solver_state->blocked[var->role_array_index].get_solver_var()),
                                              tmp_bool.get_solver_var());
                emit_assignment(node, tmp_bool, update_guard);


                //FIXME: ADD NONDET LEAF RESTRICTION HERE!
                //VAR VALUE UPDATE
                variable old_var_val = node->solver_state->vars[var->role_array_index];
                variable new_var_val = old_var_val.createFrom();
                TExpr guarded_val = solver->createCondExpr(tmp_bool.get_solver_var(),
                                                           new_var_val.get_solver_var(),
                                                           old_var_val.get_solver_var());
                emit_assignment(node, new_var_val, guarded_val);
                node->solver_state->vars[var->role_array_index] = new_var_val;

                //NEW VAR VALUE ASSUMPTIONS
                TExpr value_was_changed = solver->createNotExpr(solver->createEqExpr(old_var_val.get_solver_var(),
                                                                                     new_var_val.get_solver_var()));
                TExpr value_invariant = get_variable_invariant_from_node(node, var);
                TExpr assumption_body = solver->createImplExpr(tmp_bool.get_solver_var(),
                                                               solver->createAndExpr(value_was_changed,
                                                                                     value_invariant));
                emit_assumption(node, assumption_body);

                //SAVE THE FACT THAT THE VARIABLE HAS BEEN CHANGED
                variable old_updated_in_subrun = node->solver_state->updated_in_subrun[var->role_array_index];
                variable new_updated_in_subrun = old_updated_in_subrun.createFrom();
                TExpr new_updated_value = solver->createCondExpr(tmp_bool.get_solver_var(),
                                                                 one,
                                                                 old_updated_in_subrun.get_solver_var());
                emit_assignment(node, new_updated_in_subrun, new_updated_value);
                node->solver_state->updated_in_subrun[var->role_array_index] = new_updated_in_subrun;

                //RETURN TO CREATE THE REFINEABLE CHECK
                return tmp_bool;
            }

            void save_refineable_condition(tree &node, std::list<variable> &update_guards) {
                if (!node->is_leaf()) {
//                    log->warn("setting to false refinement of internal node {}", node->uid);
                    strict_emit_assignment(node->solver_state->refineable, zero);
                } else {
//                    log->warn("setting to false refinement of internal node {}", node->uid);
                    TExpr not_skipping = solver->createNotExpr(node->solver_state->skip.get_solver_var());
                    TExpr at_least_one_updated = solver->createFalse();
                    for (auto &&update_guard : update_guards) {
                        at_least_one_updated = solver->createOrExpr(at_least_one_updated,
                                                                    update_guard.get_solver_var());
                    }

                    //!SKIP && \/_{var} nondet_guard_var
                    TExpr refineable = solver->createAndExpr(not_skipping,
                                                             at_least_one_updated);

                    if (node->triggers.check_gap) {
                        // Force the nondeterministic assignment of the node
                        emit_comment("Forcing nondet assignment");
                        strict_emit_assignment(node->solver_state->refineable, one);
                    }

                    strict_emit_assignment(node->solver_state->refineable, refineable);
                }
            }

            void update_unblocked_vars_a(tree &node) {
                emit_comment("Begin_nondet_assignment");
                std::list<variable> update_guards;
                for (auto &&var :node->node_infos.atoms_a) {
                    variable update_guard = update_var_a(node, var);
                    update_guards.push_back(update_guard);
                }

                save_refineable_condition(node, update_guards);
                emit_comment("End_nondet_assignment");
            }

            // TRANS RELATED FUNCTIONS
            TExpr get_rule_assumptions_c(tree &node, int rule_id, TExpr &rule_is_selected) {
                rulep rule = node->node_infos.rules_c[rule_id];
                TExpr rule_precondition = generateSMTFunction(solver,
                                                              rule->prec,
                                                              node->solver_state->vars,
                                                              "");
                TExpr target_not_blocked =
                        solver->createNotExpr(
                                node->solver_state->blocked[rule->target->role_array_index].get_solver_var());
                TExpr rule_target_value = rule->is_ca ? one : zero;
                TExpr target_is_changed =
                        solver->createNotExpr(
                                solver->createEqExpr(
                                        node->solver_state->vars[rule->target->role_array_index].get_solver_var(),
                                        rule_target_value));
                //IF THE RULE IS SELECTED THEN ALL THE PRECONDITIONS MUST HOLDS
                TExpr final_assumption =
                        solver->createImplExpr(rule_is_selected,
                                               solver->createAndExpr(rule_precondition,
                                                                     solver->createAndExpr(target_not_blocked,
                                                                                           target_is_changed)));
                return final_assumption;
            }

            void apply_rule_effect_c(tree &node, int rule_id, TExpr &rule_is_selected) {
                rulep rule = node->node_infos.rules_c[rule_id];
                atomp target = rule->target;
                // UPDATE VAR VALUE
                variable old_var_var = node->solver_state->vars[target->role_array_index];
                variable new_var_var = old_var_var.createFrom();
                TExpr rule_value = rule->is_ca ? one : zero;
                TExpr new_var_value = solver->createCondExpr(rule_is_selected,
                                                             rule_value,
                                                             old_var_var.get_solver_var());
                emit_assignment(node, new_var_var, new_var_value);
                node->solver_state->vars[target->role_array_index] = new_var_var;

                // UPDATE UPDATED_IN_SUBRUN VALUE
                variable old_updated_in_subrun_var = node->solver_state->updated_in_subrun[target->role_array_index];
                variable new_updated_in_subrun_var = old_updated_in_subrun_var.createFrom();
                TExpr new_updated_value = solver->createCondExpr(rule_is_selected,
                                                                 one,
                                                                 old_updated_in_subrun_var.get_solver_var());
                emit_assignment(node, new_updated_in_subrun_var, new_updated_value);
                node->solver_state->updated_in_subrun[target->role_array_index] = new_updated_in_subrun_var;

                //SAVE VAR_ID
                variable& var_id_var = node->solver_state->var_id;
                TExpr rule_var_id_value = solver->createBVConst(rule->target->role_array_index, var_id_var.bv_size);
                TExpr rule_selected_impl_var_id = solver->createImplExpr(rule_is_selected,
                                                                         solver->createEqExpr(var_id_var.get_solver_var(),
                                                                                              rule_var_id_value));
                emit_assumption(node, rule_selected_impl_var_id);

//                //SAVE VAR_ID
//                variable old_var_id_var = node->solver_state->var_id;
//                variable new_var_id_var = node->solver_state->var_id.createFrom();
//                TExpr rule_var_id_value = solver->createBVConst(rule->target->role_array_index, old_var_id_var.bv_size);
//                TExpr new_var_id_value = solver->createCondExpr(rule_is_selected,
//                                                                rule_var_id_value,
//                                                                old_var_id_var.get_solver_var());
//                emit_assignment(node, new_var_id_var, new_var_id_value);
//                node->solver_state->var_id = new_var_id_var;
            }

            void simulate_rule_c(tree &node, int rule_id) {
                const rulep& rule = node->node_infos.rules_c[rule_id];
                if (std::is_same<TExpr, z3::expr>::value) {
                    TVar v = solver->createBVVar("Simulating_rule_" + rule->to_new_string(), node->solver_state->rule_id.bv_size);
                    TExpr value = solver->createBVConst(rule_id, node->solver_state->rule_id.bv_size);
                    solver->assertLater(solver->createEqExpr(v, value));
                }
                TExpr rule_is_selected =
                        solver->createEqExpr(node->solver_state->rule_id.get_solver_var(),
                                             solver->createBVConst(rule_id, node->solver_state->rule_id.bv_size));

                TExpr rule_assumptions = get_rule_assumptions_c(node, rule_id, rule_is_selected);

                emit_assumption(node, rule_assumptions);

                apply_rule_effect_c(node, rule_id, rule_is_selected);
            }

            void select_one_rule_c(tree &node) {
                //TO BE SURE TO APPLY AT LEAST ONE RULE WE HAVE TO ASSUME THAT RULE_ID <= RULE_COUNT
                TExpr assumption = solver->createLtExpr(node->solver_state->rule_id.get_solver_var(),
                                                        solver->createBVConst((int) node->node_infos.rules_c.size(),
                                                                              node->solver_state->rule_id.bv_size));
                emit_assumption(node, assumption);
            }

            void transition_c(tree &node) {
                emit_comment("Begin_transition_" + node->uid);
                select_one_rule_c(node);
                for (int rule_id = 0; rule_id < node->node_infos.rules_c.size(); ++rule_id) {
                    simulate_rule_c(node, rule_id);
                }
                emit_comment("End_transition_" + node->uid);
            }

            // CHILDREN RELATED FUNCTIONS
            void set_skip(tree &node) {
                tmp_bool = tmp_bool.createFrom();
                TExpr skip_child_value = tmp_bool.get_solver_var();

                for (auto &&w_ancestor :node->ancestors) {
                    tree ancestor = w_ancestor.lock();
                    skip_child_value = solver->createOrExpr(skip_child_value,
                                                            ancestor->solver_state->skip.get_solver_var());
                }

                if (node->triggers.no_skip) {
                    emit_comment("Apply this node");
                    strict_emit_assignment(node->solver_state->skip, zero);
                }

                strict_emit_assignment(node->solver_state->skip, skip_child_value);
                strict_emit_assignment(node->solver_state->guard,
                                       solver->createNotExpr(node->solver_state->skip.get_solver_var()));
            }

            void update_node_var_blocked_after_child(tree &node, tree &child) {
                variable child_applied = child->solver_state->guard;
                for (auto &&atom : node->node_infos.atoms_a) {
                    int i = atom->role_array_index;
                    variable old_blocked_node_i = node->solver_state->blocked[i];
                    TExpr is_right_target = solver->createEqExpr(child->solver_state->var_id.get_solver_var(),
                                                                 solver->createBVConst(i,
                                                                                       child->solver_state->var_id.bv_size));

                    TExpr should_apply = solver->createAndExpr(child_applied.get_solver_var(),
                                                               is_right_target);

                    TExpr new_blocked_value_i = solver->createCondExpr(should_apply,
                                                                       one,
                                                                       old_blocked_node_i.get_solver_var());

                    variable new_blocked_node_i = old_blocked_node_i.createFrom();
                    emit_assignment(node, new_blocked_node_i, new_blocked_value_i);
                    node->solver_state->blocked[i] = new_blocked_node_i;
                }
            }

            void simulate_child(tree &node, tree &child) {
                set_empty_node_state(child);

                set_skip(child);
                copy_mask(child, node->solver_state->vars, child->solver_state->vars);
                copy_mask(child, node->solver_state->blocked, child->solver_state->blocked);


                subrun(child);

                cond_save_mask(node,
                               child->solver_state->vars,
                               node->solver_state->vars,
                               child->solver_state->guard.get_solver_var());
                cond_save_ored_mask(node,
                                    node->solver_state->updated_in_subrun,
                                    child->solver_state->updated_in_subrun,
                                    node->solver_state->updated_in_subrun,
                                    child->solver_state->guard.get_solver_var());

                update_node_var_blocked_after_child(node, child);
            }

            void simulate_children(tree &node) {
                for (auto &&child :node->refinement_blocks) {
                    emit_comment("Begin_child_" + child->uid + "_simulation");
                    simulate_child(node, child);
                    emit_comment("End_child_" + child->uid + "_simulation");
                }
            }

            // EXPLORATION_STRATEGY RELATED FUNCTIONS
            //TODO: exlude atom from priorities if not in atom_a
            TExpr es_skipped_last_child(tree &node) {
                if (node->refinement_blocks.empty()) {
                    throw unexpected_error("exploration_strategy cannot be called on leaves", __FILE__, __LINE__, __FUNCTION__, __PRETTY_FUNCTION__);
                }
                std::weak_ptr<proof_node<b_solver_state>> w_last_child = node->refinement_blocks.back();
                variable last_skip = w_last_child.lock()->solver_state->skip;

                TExpr skipped = last_skip.get_solver_var();

                TExpr all_set = one;

                for (auto &&atom : node->node_infos.all_atoms) {
                    int i = atom->role_array_index;
                    variable updated_in_subrun_node_i = node->solver_state->updated_in_subrun[i];
                    variable blocked_node_i = node->solver_state->blocked[i];

                    TExpr upd_impl_block_node_i = solver->createImplExpr(updated_in_subrun_node_i.get_solver_var(),
                                                                         blocked_node_i.get_solver_var());

                    all_set = solver->createAndExpr(all_set, upd_impl_block_node_i);
                }

                TExpr final_impl = solver->createAndExpr(skipped, all_set);

                return final_impl;
            }

//            TExpr if_not_in_p_set_then_p_is_set_root(tree &node) {
//                // HERE I STATICALLY KNOW THE PRIORITY
//                const std::set<atomp>& priority = node->infos->invariant->atoms();
//                TExpr exists_not_in_priority_set = zero;
//
//                for (auto &&atom : node->infos->atoms) {
//                    if (!contains(priority, atom)) {
//                        variable atom_updated_in_subrun = node->solver_state->updated_in_subrun[atom->role_array_index];
//                        variable atom_blocked = node->solver_state->blocked[atom->role_array_index];
//                        TExpr not_in_priority_and_set_i = solver->createAndExpr(atom_updated_in_subrun.get_solver_var(),
//                                                                                atom_blocked.get_solver_var());
//                        exists_not_in_priority_set = solver->createOrExpr(exists_not_in_priority_set,
//                                                                          not_in_priority_and_set_i);
//                    }
//                }
//
////                TExpr priority_is_set = all_priority_set(node);
//                TExpr priority_is_set = one;
//                for (auto &&atom : priority) {
//                    variable atom_updated_in_subrun = node->solver_state->updated_in_subrun[atom->role_array_index];
//                    variable atom_blocked = node->solver_state->blocked[atom->role_array_index];
//                    TExpr updated_atom_is_set = solver->createImplExpr(atom_updated_in_subrun.get_solver_var(),
//                                                                       atom_blocked.get_solver_var());
//                    priority_is_set = solver->createAndExpr(priority_is_set,
//                                                            updated_atom_is_set);
//                }
//
//                TExpr if_not_in_p_set_then_p_is_set = solver->createImplExpr(exists_not_in_priority_set,
//                                                                             priority_is_set);
//                return if_not_in_p_set_then_p_is_set;
//            }

            // OLD EXPLORATION STRATEGY FUNCTIONS
//            TExpr var_not_in_priority_and_set(tree &node, int idx) {
//                variable rule_id_node = node->solver_state->rule_id;
//
//                //THIS CREATES A BIG OR ON THE SET OF RULES OF (RULE_ID_NODE = R.ID)
//                TExpr in_priority_node = zero;
//                for (int i = 0; i < node->infos->rules.size(); ++i) {
//                    const rulep &rule = node->infos->rules[i];
//                    if (contains(rule->prec->atoms(), policy->atoms(idx))) {
//                        TExpr rule_cond = solver->createEqExpr(rule_id_node.get_solver_var(),
//                                                               solver->createBVConst(i, rule_id_node.bv_size));
//                        in_priority_node = solver->createOrExpr(in_priority_node,
//                                                                rule_cond);
//                    }
//                }
//
//                //THIS CREATES A BIG OR ON THE SET OF CHILDREN TARGETS (!SKIP_CHILD /\ (IDX = VAR_ID_CHILD))
//                TExpr set_by_children_not_last = zero;
//                std::weak_ptr<gblock<simple_block_info<b_solver_info>, b_solver_state>> w_last_child = node->refinement_blocks.back();
//                auto last_child = w_last_child.lock();
//                for (std::weak_ptr<gblock<simple_block_info<b_solver_info>, b_solver_state>> &&w_child :node->refinement_blocks) {
//                    auto child = w_child.lock();
//                    if (child == last_child) {
//                        continue;
//                    }
//                    TExpr var_idx = solver->createBVConst(idx, node->solver_state->var_id.bv_size);
//                    TExpr var_id_child = child->solver_state->var_id.get_solver_var();
//                    TExpr not_skip_child = solver->createNotExpr(child->solver_state->skip.get_solver_var());
//                    TExpr is_set_by_child = solver->createAndExpr(not_skip_child,
//                                                                  solver->createEqExpr(var_idx, var_id_child));
////                                                                  solver->createAndExpr(var_idx, var_id_child));
//                    set_by_children_not_last = solver->createOrExpr(set_by_children_not_last,
//                                                                    is_set_by_child);
//                }
//
//                TExpr not_in_priority = solver->createNotExpr(in_priority_node);
//
//                TExpr not_in_priority_and_set_cond = solver->createAndExpr(not_in_priority, set_by_children_not_last);
//
//                return not_in_priority_and_set_cond;
//            }

//            TExpr all_priority_set(tree &node) {
//                variable rule_id_node = node->solver_state->rule_id;
//                TExpr global_priority_set = one;
//                // in_priority_i ==> updated_in_subrun_i ==> blocked_i
//                for (auto &&atom : node->infos->atoms) {
//                    int i = atom->role_array_index;
//                    //IN_PRIORITY_i
//                    TExpr in_priority_node = zero;
//                    for (int j = 0; j < node->infos->rules.size(); ++j) {
//                        const rulep &rule = node->infos->rules[j];
//                        if (contains(rule->prec->atoms(), policy->atoms(i))) {
//                            TExpr rule_cond = solver->createEqExpr(rule_id_node.get_solver_var(),
//                                                                   solver->createBVConst(j, rule_id_node.bv_size));
//                            in_priority_node = solver->createOrExpr(in_priority_node,
//                                                                    rule_cond);
//                        }
//                    }
//
//                    variable updated_in_subrun_node_i = node->solver_state->updated_in_subrun[i];
//                    variable blocked_node_i = node->solver_state->blocked[i];
//
//                    TExpr priority_updated_must_be_set_i =
//                            solver->createImplExpr(in_priority_node,
//                                                   solver->createImplExpr(updated_in_subrun_node_i.get_solver_var(),
//                                                                          blocked_node_i.get_solver_var()));
//
//                    global_priority_set = solver->createAndExpr(global_priority_set,
//                                                                priority_updated_must_be_set_i);
//                }
//
//                return global_priority_set;
//            }

//            TExpr if_not_in_p_set_then_p_is_set(tree &node) {
//                TExpr exists_not_in_priority_set = zero;
//                for (auto &&atom : node->infos->atoms) {
//                    int i = atom->role_array_index;
//                    TExpr not_in_priority_and_set_i = var_not_in_priority_and_set(node, i);
////                    log->warn("Atom {} -------------------------------", *policy->atoms(i));
////                    solver->printExpr(not_in_priority_and_set_i);
//                    exists_not_in_priority_set = solver->createOrExpr(exists_not_in_priority_set,
//                                                                      not_in_priority_and_set_i);
//                }
//
//                TExpr priority_is_set = all_priority_set(node);
//
//                TExpr if_not_in_p_set_then_p_is_set = solver->createImplExpr(exists_not_in_priority_set,
//                                                                             priority_is_set);
//                return if_not_in_p_set_then_p_is_set;
//            }

//            TExpr not_skipped_last_child(tree &node) {
//                if (node->refinement_blocks.empty()) {
//                    throw unexpected_error("exploration_strategy cannot be called on leaves");
//                }
//                std::weak_ptr<gblock<simple_block_info<b_solver_info>, b_solver_state>> w_last_child = node->refinement_blocks.back();
//                variable last_skip = w_last_child.lock()->solver_state->skip;
//                TExpr budget_expired = solver->createNotExpr(last_skip.get_solver_var());
//
//                TExpr if_not_in_p_set_then_p_is_set_val = zero;
//
//                //FIXME: Consider removing this
//                if(node->is_root()) {
//                    if_not_in_p_set_then_p_is_set_val = if_not_in_p_set_then_p_is_set_root(node);
//                } else {
//                    if_not_in_p_set_then_p_is_set_val = if_not_in_p_set_then_p_is_set(node);
//                }
//
//                TExpr final_cond = solver->createAndExpr(budget_expired, if_not_in_p_set_then_p_is_set_val);
//
//                return final_cond;
//            }

//            void exploration_strategy(tree &node) {
//                TExpr if_skipped = es_skipped_last_child(node);
////                log->warn("If Skipped----------------------------------------------------------------");
////                solver->printExpr(if_skipped);
//
////                log->warn("NOT SKIPPED-----------------------------------------------------------------");
//                TExpr if_budget = not_skipped_last_child(node);
////                solver->printExpr(if_budget);
//
//                TExpr global_assumption = solver->createOrExpr(if_skipped, if_budget);
//
//                emit_assumption(node, global_assumption);
//            }

            // NEW MULTY PRIORITY EXPLORATION STRATEGY FUNCTIONS
            //TODO: exlude atom from priorities if not in atom_a
            void set_priority_mask(tree& node) {
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable atom_priority = node->solver_state->priority[atom->role_array_index];
                    TExpr in_priority_condition = zero;
                    for (int i = 0; i < node->node_infos.rules_c.size(); ++i) {
                        const rulep& rule = node->node_infos.rules_c[i];
                        if (contains(rule->prec->atoms(), atom)) {
                            TExpr is_selected =
                                    solver->createEqExpr(solver->createBVConst(i, node->solver_state->rule_id.bv_size),
                                                         node->solver_state->rule_id.get_solver_var());
                            in_priority_condition = solver->createOrExpr(in_priority_condition,
                                                                         is_selected);
                        }
                    }
                    emit_assignment(node, atom_priority, in_priority_condition);
                }
            }

            void set_blocked_by_children_mask(tree &node) {
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable atom_blocked_by_children = node->solver_state->blocked_by_children[atom->role_array_index];
                    TExpr blocked_by_child_condition = zero;
                    //SKIPPING LAST CHILD
                    for (int i = 0; i < node->refinement_blocks.size() - 1; ++i) {
                        tree& child = node->refinement_blocks[i];
                        TExpr atom_blocked_by_child_i =
                                    solver->createEqExpr(solver->createBVConst(atom->role_array_index,
                                                                               child->solver_state->var_id.bv_size),
                                                         child->solver_state->var_id.get_solver_var());
                        blocked_by_child_condition = solver->createOrExpr(blocked_by_child_condition,
                                                                          atom_blocked_by_child_i);
                    }
                    emit_assignment(node, atom_blocked_by_children, blocked_by_child_condition);
                }
            }

            void set_priority_not_blocked(tree &node) {
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable& priority_atom = node->solver_state->priority[atom->role_array_index];
                    variable& blocked_atom = node->solver_state->blocked_by_children[atom->role_array_index];
                    TExpr priority_not_blocked_atom_value = solver->createAndExpr(priority_atom.get_solver_var(),
                                                                             solver->createNotExpr(blocked_atom.get_solver_var()));
                    variable& priority_not_blocked_atom = node->solver_state->priority_not_blocked[atom->role_array_index];
                    emit_assignment(node, priority_not_blocked_atom, priority_not_blocked_atom_value);
                }
            }

            void get_second_priority_by_p_not_blocked(tree &node) {
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable& second_priority_node = node->solver_state->second_priority[atom->role_array_index];
                    TExpr second_priority_atom_value = zero;
                    for (int i = 0; i < node->refinement_blocks.size() - 1; ++i) {
                        tree& child = node->refinement_blocks[i];
                        variable& child_prio_not_block_atom = child->solver_state->priority_not_blocked[atom->role_array_index];
                        second_priority_atom_value = solver->createOrExpr(second_priority_atom_value,
                                                                          child_prio_not_block_atom.get_solver_var());
                    }
                    emit_assignment(node, second_priority_node, second_priority_atom_value);
                }
            }

            TExpr multi_priority_second_priority_set(tree &node) {
                TExpr second_priority_set = one;
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable& in_second_priority = node->solver_state->second_priority[atom->role_array_index];
                    variable& updated_in_subrun = node->solver_state->updated_in_subrun[atom->role_array_index];
                    variable& set_by_child = node->solver_state->blocked_by_children[atom->role_array_index];
                    TExpr if_changed_is_set =
                            solver->createImplExpr(second_priority_set,
                                                   solver->createImplExpr(updated_in_subrun.get_solver_var(),
                                                                          set_by_child.get_solver_var()));
                    second_priority_set = solver->createAndExpr(second_priority_set,
                                                                if_changed_is_set);
                }
                return second_priority_set;
            }

            TExpr multi_priority_var_not_in_priority_nor_second_and_set(tree &node) {
                TExpr exists_var_set_not_in_p_not_snd = zero;
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable& atom_in_priority = node->solver_state->priority[atom->role_array_index];
                    variable& atom_in_second_priority = node->solver_state->second_priority[atom->role_array_index];
                    variable& atom_blocked_by_child = node->solver_state->blocked_by_children[atom->role_array_index];
                    TExpr not_priority_nor_snd_set =
                            solver->createAndExpr(solver->createAndExpr(solver->createNotExpr(atom_in_priority.get_solver_var()),
                                                                        solver->createNotExpr(atom_in_second_priority.get_solver_var())),
                                                  atom_blocked_by_child.get_solver_var());
                    exists_var_set_not_in_p_not_snd = solver->createOrExpr(exists_var_set_not_in_p_not_snd,
                                                                           not_priority_nor_snd_set);
                }
                return exists_var_set_not_in_p_not_snd;
            }

            TExpr multi_priority_if_not_in_p_nor_snd_set_then_snd_is_set(tree &node) {
                TExpr not_in_p_nor_snd_set = multi_priority_var_not_in_priority_nor_second_and_set(node);
                TExpr snd_p_set = multi_priority_second_priority_set(node);
                TExpr if_not_p_nor_snd_then_p_set = solver->createImplExpr(not_in_p_nor_snd_set,
                                                                           snd_p_set);
                return if_not_p_nor_snd_then_p_set;
            }

            TExpr multi_priority_all_priority_set(tree &node) {
                TExpr all_priority_set = one;
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable& in_priority = node->solver_state->priority[atom->role_array_index];
                    variable& updated_in_subrun = node->solver_state->updated_in_subrun[atom->role_array_index];
                    variable& set_by_child = node->solver_state->blocked_by_children[atom->role_array_index];
                    TExpr if_changed_is_set =
                            solver->createImplExpr(in_priority.get_solver_var(),
                                                   solver->createImplExpr(updated_in_subrun.get_solver_var(),
                                                                          set_by_child.get_solver_var()));
                    all_priority_set = solver->createAndExpr(all_priority_set,
                                                             if_changed_is_set);
                }
                return all_priority_set;
            }

            TExpr multi_priority_var_not_in_priority_and_set(tree &node) {
                TExpr exists_var_set_not_in_p = zero;
                for (auto &&atom : node->node_infos.all_atoms) {
                    variable& atom_in_priority = node->solver_state->priority[atom->role_array_index];
                    variable& atom_blocked_by_child = node->solver_state->blocked_by_children[atom->role_array_index];
                    TExpr not_priority_set =
                            solver->createAndExpr(solver->createNotExpr(atom_in_priority.get_solver_var()),
                                                  atom_blocked_by_child.get_solver_var());
                    exists_var_set_not_in_p = solver->createOrExpr(exists_var_set_not_in_p,
                                                                   not_priority_set);
                }
                return exists_var_set_not_in_p;
            }

            TExpr multi_priority_if_not_in_p_set_then_p_is_set(tree &node) {
                TExpr not_in_p_set = multi_priority_var_not_in_priority_and_set(node);
                TExpr p_set = multi_priority_all_priority_set(node);
                TExpr if_not_p_then_p_set = solver->createImplExpr(not_in_p_set,
                                                                   p_set);
                return if_not_p_then_p_set;
            }

            TExpr multi_priority_not_skipped_last_child(tree& node) {
                if (node->refinement_blocks.empty()) {
                    throw unexpected_error("exploration_strategy cannot be called on leaves", __FILE__, __LINE__, __FUNCTION__, __PRETTY_FUNCTION__);
                }
                std::weak_ptr<proof_node<b_solver_state>> w_last_child = node->refinement_blocks.back();
                variable last_skip = w_last_child.lock()->solver_state->skip;
                TExpr budget_expired = solver->createNotExpr(last_skip.get_solver_var());

                TExpr if_not_in_p_set_then_p_is_set_val = zero;

//                //FIXME: Consider removing this
//                if(node->is_root()) {
//                    if_not_in_p_set_then_p_is_set_val = //if_not_in_p_set_then_p_is_set_root(node);
//                            solver->createAndExpr(if_not_in_p_set_then_p_is_set_root(node),
//                                                  multi_priority_if_not_in_p_nor_snd_set_then_snd_is_set(node));
//                } else {
                    if_not_in_p_set_then_p_is_set_val =
                            solver->createAndExpr(multi_priority_if_not_in_p_set_then_p_is_set(node),
                                                  multi_priority_if_not_in_p_nor_snd_set_then_snd_is_set(node));
//                }

                TExpr final_cond = solver->createAndExpr(budget_expired, if_not_in_p_set_then_p_is_set_val);

                return final_cond;
            }

            void multi_priority_exploration_strategy(tree &node) {
                assert(!node->is_leaf());
                set_priority_mask(node);
                set_blocked_by_children_mask(node);

                set_priority_not_blocked(node);
                get_second_priority_by_p_not_blocked(node);

                TExpr if_skipped = es_skipped_last_child(node);
//                log->warn("If Skipped----------------------------------------------------------------");
//                solver->printExpr(if_skipped);

//                log->warn("NOT SKIPPED-----------------------------------------------------------------");
                TExpr if_budget = multi_priority_not_skipped_last_child(node);
//                solver->printExpr(if_budget);


                TExpr global_assumption = solver->createOrExpr(if_skipped, if_budget);


                emit_assumption(node, global_assumption);
            }

            // SUBRUN FUNCTION
            void subrun(tree &node) {
                emit_comment("Begin_subrun_" + node->uid);
                set_zero(node, node->solver_state->updated_in_subrun);
                if (node->is_leaf()) {
                    emit_comment("Node_" + node->uid + "is_leaf");
                    update_unblocked_vars_a(node);
                    transition_c(node);
                } else {
                    emit_comment("Node_" + node->uid + "_is_internal");
                    simulate_children(node);
                    emit_comment("Begin_exploration_strategy_" + node->uid);
//                    if (true) {
//                        set_increase_budget(node);
//                    }
//                    exploration_strategy(node);
                    multi_priority_exploration_strategy(node);
                    emit_comment("End_exploration_strategy_" + node->uid);
                    transition_c(node);
                }
                emit_comment("End_subrun_" + node->uid);
            }

            // ROOT SUBRUN RELATED FUNCTIONS
            void do_not_skip_root(tree &root) {
                variable& skip_root = root->solver_state->skip;
                variable& root_guard = root->solver_state->guard;

                strict_emit_assignment(skip_root, zero);
                strict_emit_assignment(root_guard, one);
            }

            void init_root_vars(tree &root,
                                const std::set<userp, std::function<bool(const userp &, const userp &)>> &initial_confs) {
                TExpr init_expr = zero;
                for (auto &&conf : initial_confs) {
                    TExpr conf_expr = one;
                    for (auto &&atom : root->node_infos.all_atoms) {
                        TExpr init_value = contains(conf->config(), atom) ? one : zero;
                        variable root_var = root->solver_state->vars[atom->role_array_index];
                        TExpr value_saved = solver->createEqExpr(root_var.get_solver_var(), init_value);
                        conf_expr = solver->createAndExpr(conf_expr, value_saved);
                    }
                    init_expr = solver->createOrExpr(init_expr,
                                                     conf_expr);
                }
                emit_assumption(root, init_expr);
            }

            void assert_invariant_pre_subrun(tree &node) {
                assert(node->is_root());
                Expr& target_expr = node->node_infos.rules_c[0]->prec;

                TExpr invariant_expr = generateSMTFunction(solver,
                                                           target_expr,
                                                           node->solver_state->vars,
                                                           "");
                assertions.push_back(invariant_expr);
            }

            void assert_invariant_post_subrun(tree &node) {
                assert(node->is_root());
                atomp target_atom = node->node_infos.rules_c[0]->target;

                TExpr invariant_expr = generateSMTFunction(solver,
                                                           createEqExpr(createLiteralp(target_atom),
                                                                        createConstantTrue()),
                                                           node->solver_state->vars,
                                                           "");
                assertions.push_back(invariant_expr);
            }

            void root_subrun(tree &root,
                             const std::set<userp, std::function<bool(const userp &, const userp &)>> &initial_confs) {
                set_empty_node_state(root);
                do_not_skip_root(root);

                emit_comment("Variable_initialization");
                init_root_vars(root, initial_confs);

                assert_invariant_pre_subrun(root);
                set_zero(root, root->solver_state->blocked);
                subrun(root);
                assert_invariant_post_subrun(root);
            }

            // SOLVER RELATED FUNCTIONS
            void add_assertions() {
                TExpr final_assertion = zero;
                for (auto &&assertion : assertions) {
                    final_assertion = solver->createOrExpr(final_assertion,
                                                           assertion);
                }
                solver->assertLater(final_assertion);
            }

            bool is_reachable(tree &root,
                              const std::set<userp, std::function<bool(const userp &, const userp &)>> &initial_confs) {
                emit_comment("root_subrun");
                root_subrun(root, initial_confs);
                emit_comment("root_assertion");
                add_assertions();

                auto start = std::chrono::high_resolution_clock::now();

                if (Config::show_solver_statistics) {
                    solver->print_statistics();
                }

                if (!std::is_same<term_t, TExpr>::value && Config::dump_smt_formula != "") {
                    solver->printContext(Config::dump_smt_formula);
                    log->info("BMC SMT formula dumped at: {}", Config::dump_smt_formula);
                }

                SMTResult res = solver->solve();

                if (std::is_same<term_t, TExpr>::value && Config::dump_smt_formula != "") {
                    solver->printContext(Config::dump_smt_formula);
                    log->info("BMC SMT formula dumped at: {}", Config::dump_smt_formula);
                    if (res == SAT) {
                        solver->printModel();
                    }
                }

                auto end = std::chrono::high_resolution_clock::now();
                auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
                log->debug("------------ SMT SOLVED IN {} ms ------------", milliseconds.count());

                return res == SAT;
            }

            bool set_node_refinement_from_model(tree &node) {
                if (!node->is_leaf()) {
                    bool refineable_subtree = false;
                    for (auto &&child : node->refinement_blocks) {
                        refineable_subtree = set_node_refinement_from_model(child) || refineable_subtree;
                    }
                    return refineable_subtree;
                } else {
                    bool is_node_refineable = node->leaf_infos->gap == leaves_infos::YES;
                    if (node->leaf_infos->gap == leaves_infos::UNKNOWN) {
                        is_node_refineable = solver->get_bool_value(node->solver_state->refineable.get_solver_var());
                        node->leaf_infos->gap = is_node_refineable ? leaves_infos::YES : leaves_infos::UNKNOWN;
                    }
                    if (is_node_refineable) {
                        log->warn("Node {} is refineable", node->uid);
                    }

//                    bool increase_node_budget = solver->get_bool_value(node->solver_state->increase_budget.get_solver_var());
//                    node->infos->solverInfo->increase_budget = increase_node_budget ? b_solver_info::YES : b_solver_info::NO;
//                    if (node->infos->solverInfo->increase_budget == b_solver_info::YES) {
//                        log->warn("Node {} budget should be increased", node->uid);
//                    }
                    return is_node_refineable;
                }
            }

            bool anotate_refineable(tree &root) {
                return set_node_refinement_from_model(root);
            }

            void initialize() {
                tmp_bool = variable("tmp_bool", 0, 1, solver.get(), BOOLEAN);
                zero = solver->createBoolConst(0);
                one = solver->createBoolConst(1);
            }

        public:

            tree_to_SMT(const std::shared_ptr<arbac_policy> _policy,
                        std::shared_ptr<SMTFactory<TVar, TExpr>> _solver) :
                    policy(_policy),
                    solver(std::move(_solver)),
                    assertions(std::list<TExpr>()) { }

            over_analysis_result translate_and_run(tree &root,
                                                   const std::set<userp, std::function<bool(const userp &, const userp &)>> &initial_confs) {
                solver->deep_clean();
                initialize();
                bool result = is_reachable(root, initial_confs);
                if (result) {
                    bool is_refineable = anotate_refineable(root);
                    if (!is_refineable) {
                        return UNSAFE;
                    } else {
                        return UNSAFE_REFINEABLE;
                    }
                } else {
                    return SAFE;
                }
            }

        };

//        class tree_pruner {
//        private:
//            tree& tree;
//
//        public:
//            tree_pruner(tree &_tree) :
//                    tree(_tree){ }
//        };

        const OverapproxOptions overapprox_strategy;

//        std::shared_ptr<b_solver_info> get_empty_solver_info() {
//            b_solver_info ret_body;
//            ret_body.refineable = b_solver_info::UNSET;
//            ret_body.increase_budget = b_solver_info::UNSET;
//            return std::make_shared<b_solver_info>(ret_body);
//        }

        static void update_leaves_infos(tree& tree) {
            if (!tree->is_leaf()) {
                return;
            }
            if (tree->leaf_infos == nullptr) {
                throw unexpected_error("All leaves must have the associated leaf_infos != nullptr",
                                       __FILE__, __LINE__, __FUNCTION__, __PRETTY_FUNCTION__);
            }

            std::map<atomp, std::set<bool>>& map = tree->leaf_infos->nondet_restriction;

            for (auto &&atom : tree->node_infos.all_atoms) {
                std::set<bool> new_set;
                // ADDING POSSIBLE VALUES DEPENDING ON rules_A
                for (auto &&rule_a : tree->node_infos.rules_a) {
                    if (rule_a->target == atom) {
                        new_set.insert(rule_a->is_ca ? true : false);
                    }
                }
                // REMOVING VALUES FORBIDDEN BY INVARIANTS
                for (auto &&invariant : tree->invariants.inv_A_C.get_as_map()) {
//                    if () {
//
//                    }
                    assert(false);
                }
            }

        }

        void consolidate_tree(tree& tree) {

        }

        int get_budget() { // std::shared_ptr<simple_block_info<b_solver_info>>& info) {
            return overapprox_strategy.blocks_count;
//            int max_budget = 0;
//            for (auto &&rule :info->rules) {
//                if (max_budget < rule->prec->atoms().size())
//                    max_budget = (int)rule->prec->atoms().size();
//            }
//            return max_budget + 1;
//            switch (overapprox_strategy.blocks_strategy) {
//                case OverapproxOptions::STRICT_BLOCK:
//                    return overapprox_strategy.blocks_count;
//                    break;
//                case OverapproxOptions::AT_MOST_BLOCK:
//                    return 1;
//                    break;
//                case OverapproxOptions::AT_LEAST_BLOCK:
//                    return overapprox_strategy.blocks_count;
//                    break;
//                default:
//                    throw unexpected_error("missing cases in switch on overapprox_strategy.blocks_strategy");
//            }
        }

        void tree_clean_solver_info_state(tree& node) {
            node->solver_state = nullptr;
//            node->node_infos->solverInfo->refineable = b_solver_info::UNSET;
            for (auto &&child :node->refinement_blocks) {
                tree_clean_solver_info_state(child);
            }
        }

        bool refine_tree(tree& _node) {
            if (!_node->is_leaf()) {
                bool changed = false;
                for (auto &&child :_node->refinement_blocks) {
                    changed = refine_tree(child) || changed;
                }
                return changed;
            } else if (overapprox_strategy.depth_strategy == OverapproxOptions::AT_MOST_DEPTH &&
                    _node->depth >= overapprox_strategy.depth) {
                return false;
            } else {
                if (_node->leaf_infos->gap == leaves_infos::YES) {
                    int i = 0;
                    tree_path first_path = _node->path;
                    first_path.push_back(i);
                    node_policy_infos node_info(_node->node_infos.rules_a,
                                                _node->node_infos.rules_a,
                                                _node->node_infos.all_atoms,
                                                _node->node_infos.atoms_a,
                                                _node->node_infos.policy_atoms_count);
                    std::unique_ptr<leaves_infos> leaf_infos(new leaves_infos());
                    std::list<std::weak_ptr<proof_node<b_solver_state>>> prec_ancestors = _node->ancestors;
                    prec_ancestors.push_back(_node);
                    int j = 0;
                    for (auto &&anc : first_path) {
                        log->critical(anc);
                    }
                    tree actual_child(new node(first_path,
                                               _node->depth + 1,
                                               node_info,
                                               std::move(leaf_infos),
                                               prec_ancestors,
                                               _node));
                    _node->refinement_blocks.push_back(actual_child);

                    j = 0;
                    for (auto &&anc : first_path) {
                        log->critical(anc);
                    }

                    int budget = get_budget();
                    for (++i; i < budget; ++i) {
                        first_path = _node->path;
                        first_path.push_back(i);
                        node_policy_infos act_info(_node->node_infos.rules_a,
                                                   _node->node_infos.rules_a,
                                                   _node->node_infos.all_atoms,
                                                   _node->node_infos.atoms_a,
                                                   _node->node_infos.policy_atoms_count);
                        leaf_infos = std::make_unique<leaves_infos>(_node->leaf_infos->clone());

                        prec_ancestors = actual_child->ancestors;
                        prec_ancestors.push_back(actual_child);
                        actual_child = tree(new node(first_path,
                                                     _node->depth + 1,
                                                     act_info,
                                                     std::move(leaf_infos),
                                                     prec_ancestors,
                                                     _node));
                        _node->refinement_blocks.push_back(actual_child);
                    }
                    _node->leaf_infos = nullptr;
                    return true;
                } else {
                    return false;
                }
            }
        }

        void create_target_fake_roles(const std::shared_ptr<arbac_policy>& policy) {
            if (target_atom == nullptr) {
                target_atom = createAtomp("__overapprox__target__", policy->atom_count(), 1);
            }
            if (fake_atom == nullptr) {
                fake_atom = createAtomp("__overapprox__fake__", policy->atom_count() + 1, 1);
            }
        };

        tree create_tree_root(const int policy_atom_count,
                              const std::vector<atomp>& atoms,
                              const std::vector<rulep>& rules,
                              const Expr& to_check) {

            rulep root_rule(new rule(true,
                                     createConstantTrue(),
                                     to_check,
                                     target_atom,
                                     -1));

            std::vector<rulep> root_rule_c;
            root_rule_c.push_back(std::move(root_rule));

            std::vector<atomp> all_atoms = atoms;
            all_atoms.push_back(target_atom);

            node_policy_infos root_info(rules,
                                        std::move(root_rule_c),
                                        std::move(all_atoms),
                                        atoms,
                                        policy_atom_count + 2);

            std::unique_ptr<leaves_infos> root_leaf_infos(new leaves_infos());

            tree root(new node(std::list<int>(),
                               0,
                               root_info,
                               std::move(root_leaf_infos),
                               std::list<std::weak_ptr<node>>(),
                               std::weak_ptr<node>()));

            return root;
        }

        std::pair<std::vector<atomp>, std::vector<rulep>> slice_policy(const std::vector<atomp>& orig_atoms,
                                                                       const std::vector<rulep>& orig_rules,
                                                                       const Expr& target) {
            bool fixpoint = false;
            std::set<rulep> rules_to_save_set;
            std::set<atomp> necessary_atoms_set;
            necessary_atoms_set.insert(target->atoms().begin(), target->atoms().end());
            while (!fixpoint) {
                fixpoint = true;
                for (auto &&rule : orig_rules) {
//                    print_collection(necessary_atoms);
//                    print_collection(to_save);
//                    std::cout << *rule << ": "_atoms);
//                    print_collection(to_save);
//                    std::cout << *rule << ": " << (!contains(to_save, rule) && contains(necessary_atoms, rule->target)) << std::endl;
                    if (!contains(rules_to_save_set, rule) && contains(necessary_atoms_set, rule->target)) {
//                        print_collection(rule->admin->literals());
//                        print_collection(rule->prec->literals());

//                        necessary_atoms.insert(rule->admin->atoms().begin(), rule->admin->atoms().end());
                        necessary_atoms_set.insert(rule->prec->atoms().begin(), rule->prec->atoms().end());
                        rules_to_save_set.insert(rule);
                        fixpoint = false;
                    }
                }
            }

            std::vector<rulep> rules_to_save(rules_to_save_set.begin(), rules_to_save_set.end());
            std::vector<atomp> necessary_atoms(necessary_atoms_set.begin(), necessary_atoms_set.end());

            return std::pair<std::vector<atomp>, std::vector<rulep>>(necessary_atoms, rules_to_save);
        }
    public:

        explicit learning_overapprox(OverapproxOptions strategy):
                overapprox_strategy(strategy) { }

        bool operator()(const std::shared_ptr<SMTFactory<TVar, TExpr>>& solver,
                        const std::vector<atomp>& orig_atoms,
                        const std::vector<rulep>& orig_rules,
                        const std::shared_ptr<arbac_policy>& policy,
                        const Expr& to_check) {
            std::vector<atomp> used_atoms;
            std::vector<rulep> used_rules;

            create_target_fake_roles(policy);

            log->warn("Original atoms: {}", orig_atoms.size());
            log->warn("Original rules: {}", orig_rules.size());
            if (overapprox_strategy.no_backward_slicing) {
                used_atoms = orig_atoms;
                used_rules = orig_rules;
            } else {
                auto pair = slice_policy(orig_atoms, orig_rules, to_check);
                used_atoms = pair.first;
                used_rules = pair.second;

                log->warn("Applied slicing");

                log->warn("after slicing atoms: {}", used_atoms.size());
                log->warn("after slicing rules: {}", used_rules.size());
            }

            bool completed = false;

            tree proof = create_tree_root(policy->atom_count(), used_atoms, used_rules, to_check);

            while (!completed) {
                tree_to_SMT translator(policy, solver);

                over_analysis_result result =
                        translator.translate_and_run(proof, policy->unique_configurations());

                std::pair<std::string, std::string> strs = proof->tree_to_full_string();
                log->warn("{}", strs.first);
//                log->debug("{}", strs.second);
                switch (result) {
                    case SAFE:
                        log->info("Target role is not reachable");
                        completed = true;
                        return false;
                        break;
                    case UNSAFE:
                        log->info("Target role may be reachable (but proof is not refineable)");
                        completed = true;
                        return true;
                        break;
                    case UNSAFE_REFINEABLE:
                        log->info("Target role may be reachable... REFINING");
                        bool changed = refine_tree(proof);
                        tree_clean_solver_info_state(proof);
//                        std::pair<std::string, std::string> strs = proof->tree_to_full_string();
//                        log->warn("{}", strs.first);
//                        log->debug("{}", strs.second);
                        if (!changed) {
                            log->info("Givin up refinement...");
                            completed = true;
                            return true;
                        }
                        break;
                }
            }
            throw unexpected_error("While loop should converge at some point!", __FILE__, __LINE__, __FUNCTION__, __PRETTY_FUNCTION__);
        }

    };

    template <typename TVar, typename TExpr>
    bool overapprox_learning(const std::shared_ptr<SMTFactory<TVar, TExpr>>& solver,
                             const std::shared_ptr<arbac_policy>& policy,
                             const std::vector<atomp> atoms,
                             const std::vector<rulep> rules,
                             const Expr& to_check) {
        OverapproxOptions strategy = {
            .version = OverapproxOptions::LEARNING,
            .depth_strategy = OverapproxOptions::AT_MOST_DEPTH,
            .depth = Config::overapproxOptions.depth,
            .blocks_strategy = OverapproxOptions::AT_LEAST_BLOCK,
            .blocks_count = Config::overapproxOptions.blocks_count,
            .no_backward_slicing = Config::overapproxOptions.no_backward_slicing
        };
        learning_overapprox<TVar, TExpr> overapprox(strategy);
        return overapprox(solver, atoms, rules, policy, to_check);
    }

    template bool overapprox_learning<term_t, term_t>(const std::shared_ptr<SMTFactory<term_t, term_t>>& solver,
                                                      const std::shared_ptr<arbac_policy>& policy,
                                                      const std::vector<atomp> atoms,
                                                      const std::vector<rulep> rules,
                                                      const Expr& to_check);

    template bool overapprox_learning<expr, expr>(const std::shared_ptr<SMTFactory<expr, expr>>& solver,
                                                  const std::shared_ptr<arbac_policy>& policy,
                                                  const std::vector<atomp> atoms,
                                                  const std::vector<rulep> rules,
                                                  const Expr& to_check);

    template bool overapprox_learning<BoolectorExpr, BoolectorExpr>(const std::shared_ptr<SMTFactory<BoolectorExpr, BoolectorExpr>>& solver,
                                                                    const std::shared_ptr<arbac_policy>& policy,
                                                                    const std::vector<atomp> atoms,
                                                                    const std::vector<rulep> rules,
                                                                    const Expr& to_check);

    template bool overapprox_learning<msat_term, msat_term>(const std::shared_ptr<SMTFactory<msat_term, msat_term>>& solver,
                                                            const std::shared_ptr<arbac_policy>& policy,
                                                            const std::vector<atomp> atoms,
                                                            const std::vector<rulep> rules,
                                                            const Expr& to_check);


}