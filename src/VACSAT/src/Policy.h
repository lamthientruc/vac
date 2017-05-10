//
// Created by esteffin on 24/04/17.
//

#ifndef VACSAT_POLICY_H
#define VACSAT_POLICY_H

#include <string>
#include <vector>
#include <list>
#include "Logics.h"
#include "prelude.h"

namespace SMT {

    typedef Literalp atom;

    class rule {
    public:
        rule(bool is_ca, Expr admin, Expr prec, atom target, int original_idx);

        void print() const;

        std::string to_string() const;
        std::string get_type() const;

        static rule* create_ca(Expr admin, Expr prec, atom target, int original_idx);
        static rule* create_cr(Expr admin, Expr prec, atom target, int original_idx);

        friend std::ostream& operator<< (std::ostream& stream, const rule& self);

        bool is_ca;
        Expr admin;
        Expr prec;
        atom target;
        int original_idx;
    };

    typedef std::shared_ptr<rule> rulep;

    class user {
    public:
        user(int original_idx);
        user(int original_idx, std::set<atom> config);

        void remove_atom(const atom& atom1);

        const std::string to_string() const;
        friend std::ostream& operator<< (std::ostream& stream, const user& self);

        inline const std::set<atom>& config() const {
            return _config;
        }

        static user from_policy(std::vector<atom>& atoms, int original_idx);

        const int original_idx;
        const std::string name;
    private:
        std::set<atom> _config;
    };

    typedef std::shared_ptr<user> userp;

    class arbac_policy;

//    class arbac_cache {
//    public:
//        arbac_cache(arbac_policy policy);
//        arbac_cache(std::vector<Literalp> atoms,
//                    std::vector<std::shared_ptr<rule>> can_assign_rules,
//                    std::vector<std::shared_ptr<rule>> can_revoke_rules);
//        std::list<rulep> get_assigning_r(Literalp atom);
//        std::list<rulep> get_revoking_r(Literalp atom);
//        std::list<rulep> get_ca_using_r(Literalp atom);
//        std::list<rulep> get_cr_using_r(Literalp atom);
//
//        void update(arbac_policy policy);
//
//    private:
//        std::vector<std::list<rulep>> assigning_rs;
//        std::vector<std::list<rulep>> revoking_rs;
//        std::vector<std::list<rulep>> ca_using_rs;
//        std::vector<std::list<rulep>> cr_using_rs;
//    };

    class arbac_policy {
    public:
        arbac_policy();

        inline int atom_count() const {
            return (int) _atoms.size();
        }

//        void remove_can_assign(rule to_remove);
//        void remove_can_revoke(rule to_remove);

        Expr user_to_expr(int user_id) const;

        void add_rule(const rulep& rule);
        void add_can_assign(const rulep& rule);
        void add_can_revoke(const rulep& rule);

        void remove_rule(const rulep& rule);
        void remove_can_assign(const rulep& rule);
        void remove_can_revoke(const rulep& rule);
        void remove_atom(const Literalp& atom);

        const std::string to_string() const;

        friend std::ostream& operator<< (std::ostream& stream, const arbac_policy& self);

        Literalp goal_role;

        inline const std::vector<rulep>& rules() const {
            return _rules;
        }
        inline const std::vector<rulep>& can_assign_rules() const {
            return _can_assign_rules;
        }
        inline const std::vector<rulep>& can_revoke_rules() const {
            return _can_revoke_rules;
        }
        inline const std::vector<Literalp>& atoms() const {
            return _atoms;
        }
        inline const std::vector<userp>& users() const {
            return _users;
        }
        inline const int users_to_track() const {
            return _users_to_track;
        }

        inline const rulep& rules(int i) {
            return _rules[i];
        }
        inline const rulep& can_assign_rules(int i) {
            return _can_assign_rules[i];
        }
        inline const rulep& can_revoke_rules(int i) {
            return _can_revoke_rules[i];
        }
        inline const Literalp& atoms(int i) {
            return _atoms[i];
        }
        inline const userp& users(int i) {
            return _users[i];
        }

        private:
        void update();
        void unsafe_remove_atom(const Literalp& atom);
        void unsafe_remove_rule(const rulep& rule);
        void unsafe_remove_can_assign(const rulep& rule);
        void unsafe_remove_can_revoke(const rulep& rule);

        int _users_to_track;

        std::vector<Literalp> _atoms;
        std::vector<std::shared_ptr<rule>> _rules;
        std::vector<std::shared_ptr<rule>> _can_assign_rules;
        std::vector<std::shared_ptr<rule>> _can_revoke_rules;

        std::vector<userp> _users;

//        arbac_cache cache;

    };

}

#endif //VACSAT_POLICY_H
