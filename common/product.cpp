#include "common/product.hpp"

#include <iostream>
#include <cstdlib>
#include <ctime>

// Price

Price::Price() {}

Price::Price(const std::pair<Currency, unsigned>& p)
{
    first = p.first;
    second = p.second;
}

Price::Price(Currency c, unsigned p)
{
    first = c;
    second = p;
}

Price::Price(string s)
{
    size_t comma;
    if ((comma = s.find(',')) != string::npos) {
        s.erase(comma, 1);
    }

    size_t ss_start;

    for (auto& [currency, symbol] : CURRENCY_SYMBOLS) {
        if ((ss_start = s.find(symbol)) == string::npos) { ss_start = 0; continue; }
        else {
            first = currency;
            ss_start += symbol.size();
            break;
        }
    }

    s = s.substr(ss_start);
    size_t ss_point = s.find(".");

    try {
        second = std::stoi(s.substr(0, ss_point)) * 100;
        if (ss_point != string::npos)
            second += std::stoi(s.substr(ss_point + 1));
    } catch (const std::exception& e) {
        Log(WARNING, "Exception converting string {} to Price: {}", s, e.what());
    }
}

string Price::str() const
{
    std::string result = std::to_string(second);

    result.insert(0, CURRENCY_SYMBOLS.at(first));
    if (second >= 100)
        result.insert(result.size() - 2, ".");
    else if (second >= 10)
        result.insert(result.size() - 2, "0.");
    else
        result.insert(result.size() - 1, "0.0");

    return result;
}

bool Price::operator>(Price b) { return second > b.second; }
bool Price::operator<(Price b) { return second < b.second; }
bool Price::operator==(Price b) { return second == b.second; }
Price Price::operator*(float f) { return {first, static_cast<unsigned>(second * f)}; }
Price Price::operator/(float f) { return {first, static_cast<unsigned>(second / f)}; }

PricePU::PricePU() {}

PricePU::PricePU(const std::pair<Unit, Price>& p)
{
    first = p.first;
    second = p.second;
}

PricePU::PricePU(Unit u, Price p)
{
    first = u;
    second = p;
}

PricePU::PricePU(string s)
{
    if (s.empty()) {
        first = Unit::None;
        second = {EUR, 0};
        return;
    }

    size_t ss;
    if ((ss = s.find("/")) != string::npos) {
        string u = s.substr(ss + 1);
        string p = s.substr(0, ss);
        Price pp = p;
        if (u == "kg") {
            first = Unit::Kilogrammes;
            second = pp;
        } else if (u == "75cl") {
            first = Unit::Litres;
            second = pp / 0.75;
        } else if (u == "70cl") {
            first = Unit::Litres;
            second = pp / 0.7;
        } else if (u == "l") {
            first = Unit::Litres;
            second = pp;
        } else if (u == "ml") {
            first = Unit::Litres;
            second = pp * 1000;
        } else if (u == "m²") {
            first = Unit::SqMetres;
            second = pp;
        } else {
            Log(WARNING, "Unrecognised unit for '{}'!", s);
        }
    } else if ((ss = s.find(" ")) != string::npos) {
        string u = s.substr(ss + 1);
        string p = s.substr(0, ss);

        if (u == "each") {
            first = Unit::Piece;
            second = p;
        } else {
            Log(WARNING, "Unrecognised unit for '{}'!", s);
        }
    } else {
        Log(WARNING, "Unrecognised delimiter/unit for '{}'!", s);
    }
}

string PricePU::str() const
{
    std::string result = second.str();
    const static char* UNIT_SUFFIXES[] = { "", " each", "/kg", "/l", "/m²" };

    return result + UNIT_SUFFIXES[first];
}

StoreSelection::StoreSelection() {}

StoreSelection::StoreSelection(StoreID id) { push_back(id); }

bool StoreSelection::Has(StoreID id) const
{
    for (auto it = begin(); it < end(); ++it)
        if (*it == id) return true;

    return false;
}

bool StoreSelection::Has(const StoreSelection& selection) const
{
    if (selection.size() > size()) return false;

    for (StoreID i : selection)
        if (!Has(i)) return false;

    return true;
}

void StoreSelection::Remove(StoreID id)
{
    for (auto it = begin(); it < end();)
        if (*it == id) it = erase(it);
        else ++it;
}

void StoreSelection::Remove(const StoreSelection& s)
{
    for (StoreID i : s) Remove(i);
}

void StoreSelection::Add(StoreID id)
{
    if (!Has(id)) push_back(id);
}

ProductList::ProductList(int d) : depth(d) {}

ProductList::ProductList(const Product& p)
{
    push_back({ p, QueryResultInfo {0} });
}

ProductList::ProductList(const std::vector<Product>& products, int d) : depth(d)
{
    int i;
    for (auto& p : products) {
        push_back({p, {i}});
        i++;
    }
}

void ProductList::Add(const ProductList& l)
{
    insert(end(), l.begin(), l.end());
    if (l.depth != 0 && l.depth < depth)
        depth = l.depth;
}

Product ProductList::First() const
{
    if (size()) {
        return at(0).first;
    }

    return PRODUCT_ERROR;
}

QueryTemplate ProductList::AsQueryTemplate(const string& querystr,
                                           const StoreSelection& ids) const
{
    QueryTemplate tmpl;

    tmpl.query_string = querystr;
    tmpl.stores = ids;
    tmpl.timestamp = std::time(nullptr);
    tmpl.depth = depth;

    for (auto it = begin(); it != end(); ++it) {
        tmpl.results.emplace((*it).first.id, (*it).second);
    }

    return tmpl;
}

std::vector<Product> ProductList::AsProductVector() const
{
    std::vector<Product> r;

    for (auto it = begin(); it != end(); ++it) {
        r.push_back((*it).first);
    }
    return r;
}
