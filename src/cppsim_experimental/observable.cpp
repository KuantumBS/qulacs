#include "observable.hpp"

#include <numeric>

#ifdef _USE_GPU
#include <gpusim/stat_ops.h>
#endif

#include "pauli_operator.hpp"
#include "state.hpp"
#include "type.hpp"

CPPCTYPE Observable::calc_coef(
    const MultiQubitPauliOperator& a, const MultiQubitPauliOperator& b) const {
    auto x_a = a.get_x_bits();
    auto z_a = a.get_z_bits();
    auto x_b = b.get_x_bits();
    auto z_b = b.get_z_bits();
    const size_t max_size = std::max(x_a.size(), x_b.size());
    if (x_a.size() != x_b.size()) {
        x_a.resize(max_size);
        z_a.resize(max_size);
        x_b.resize(max_size);
        z_b.resize(max_size);
    }
    CPPCTYPE res = 1.0;
    CPPCTYPE I = 1.0i;
    size_t i;
    for (i = 0; i < x_a.size(); i++) {
        if (x_a[i] && !z_a[i]) {            // a = X
            if (!x_b[i] && z_b[i]) {        // b = Z
                res *= -I;                  // XZ = -iY
            } else if (x_b[i] && z_b[i]) {  // b = Y
                res *= I;                   // XY = iZ
            }
        } else if (!x_a[i] && z_a[i]) {     // a = Z
            if (x_b[i] && !z_b[i]) {        // b = X
                res *= I;                   // ZX = iY
            } else if (x_b[i] && z_b[i]) {  // b = Y
                res *= -I;                  // ZY = -iX
            }
        } else if (x_a[i] && z_a[i]) {       // a = Y
            if (x_b[i] && !z_b[i]) {         // b = X
                res *= -I;                   // YX = -iZ
            } else if (!x_b[i] && z_b[i]) {  // b = Z
                res *= I;                    // YZ = iX
            }
        }
    }
    return res;
}

std::pair<CPPCTYPE, MultiQubitPauliOperator> Observable::get_term(
    const size_t index) const {
    return std::make_pair(
        this->_coef_list.at(index), this->_pauli_terms.at(index));
}

std::unordered_map<std::string, ITYPE> Observable::get_dict() const{
    return _term_dict;
}

void Observable::add_term(const CPPCTYPE coef, MultiQubitPauliOperator op) {
    this->_coef_list.push_back(coef);
    this->_pauli_terms.push_back(op);
    this->_term_dict[op.to_string()] = _coef_list.size() - 1;
}

void Observable::add_term(const CPPCTYPE coef, std::string s) {
    MultiQubitPauliOperator op(s);
    this->_coef_list.push_back(coef);
    this->_pauli_terms.push_back(op);
    this->_term_dict[op.to_string()] = _coef_list.size() - 1;
}

void Observable::remove_term(UINT index) {
    this->_coef_list.erase(this->_coef_list.begin() + index);
    this->_pauli_terms.erase(this->_pauli_terms.begin() + index);
}

CPPCTYPE Observable::get_expectation_value(
    const QuantumStateBase* state) const {
    CPPCTYPE sum = 0;
    for (size_t index = 0; index < this->_pauli_terms.size(); ++index) {
        sum += this->_coef_list.at(index) *
               this->_pauli_terms.at(index).get_expectation_value(state);
    }
    return sum;
}

CPPCTYPE Observable::get_transition_amplitude(const QuantumStateBase* state_bra,
    const QuantumStateBase* state_ket) const {
    CPPCTYPE sum = 0;
    for (size_t index = 0; index < this->_pauli_terms.size(); ++index) {
        sum += this->_coef_list.at(index) *
               this->_pauli_terms.at(index).get_transition_amplitude(
                   state_bra, state_ket);
    }
    return sum;
}

Observable* Observable::copy() const {
    Observable* res = new Observable();
    size_t i;
    for (i = 0; i < this->_coef_list.size(); i++) {
        res->add_term(this->_coef_list[i], *this->_pauli_terms[i].copy());
    }
    return res;
}

Observable Observable::operator+(const Observable& target) const {
    Observable res = *(this->copy());
    res += target;
    return res;
}

Observable& Observable::operator+=(const Observable& target) {
    auto u_map = target.get_dict();

    for (auto item:u_map) {
        if (_term_dict.find(item.first) != _term_dict.end()) {
            ITYPE id = _term_dict[item.first];
            this->_coef_list[id] += target.get_term(item.second).first;
        } else {
            auto term = target.get_term(item.second);
            this->add_term(term.first, term.second);
        }
    }
    return *this;
}

Observable Observable::operator-(const Observable& target) const {
    Observable res = *(this->copy());
    res -= target;
    return res;
}

Observable& Observable::operator-=(const Observable& target) {
    auto u_map = target.get_dict();

    for (auto item : u_map) {
        if (_term_dict.find(item.first) != _term_dict.end()) {
            ITYPE id = _term_dict[item.first];
            this->_coef_list[id] -= target.get_term(item.second).first;
        } else {
            auto term = target.get_term(item.second);
            this->add_term(-term.first, term.second);
        }
    }
    return *this;
}

Observable Observable::operator*(const Observable& target) const {
    Observable res;
    size_t i, j;
    for (i = 0; i < this->_pauli_terms.size(); i++) {
        for (j = 0; j < target.get_term_count(); j++) {
            Observable tmp;
            auto term = target.get_term(j);
            CPPCTYPE bits_coef = calc_coef(this->_pauli_terms[i], term.second);
            tmp.add_term(this->_coef_list[i] * term.first * bits_coef,
                this->_pauli_terms[i] * term.second);
            res += tmp;
        }
    }
    return res;
}

Observable Observable::operator*(const CPPCTYPE& target) const {
    Observable res = *(this->copy());
    res *= target;
    return res;
}

Observable& Observable::operator*=(const Observable& target) {
    Observable tmp = (*this) * target;
    this->_coef_list.clear();
    this->_pauli_terms.clear();
    size_t i;
    for (i = 0; i < tmp.get_term_count(); i++) {
        auto term = tmp.get_term(i);
        this->add_term(term.first, term.second);
    }
    return *this;
}

Observable& Observable::operator*=(const CPPCTYPE& target) {
    ITYPE i;
#pragma omp parallel for
    for (i = 0; i < (ITYPE)this->_coef_list.size(); i++) {
        this->_coef_list[i] *= target;
    }
    return *this;
}

std::string Observable::to_string() const{
    std::ostringstream ss;
    std::string res;
    size_t i;
    for (i = 0; i < get_term_count(); i++) {
        // (1.0-2.0j)
        ss << "(" << _coef_list[i].real();

        // 虚数部には符号をつける
        // +0j or -0j に対応させるためstd::showposを用いる
        // 0.の場合は+に強制する
        if (_coef_list[i].imag() != 0.)
            ss << std::showpos << _coef_list[i].imag() << "j) ";
        else
            ss << std::showpos << 0. << "j) ";

        // [X 0 Y 1 Z 2]
        ss << "[" << _pauli_terms[i].to_string() << "]";
        if (i != get_term_count() - 1) {
            ss << " +" << std::endl;
        }
        res += ss.str();
        ss.str("");
        ss.clear();
        // noshowposして、1項目以降の実数部に符号が付かないようにする
        ss << std::noshowpos;
    }
    return res;
}