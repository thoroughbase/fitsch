#pragma once

#include <string>
#include <type_traits>

#include <lexbor/html/parser.h>
#include <lexbor/dom/interfaces/element.h>

#include "common/util.hpp"

#define ELEMENT_NULL_VALUE 0x00
#define ELEMENT_HEAD_VALUE 0x01
#define ELEMENT_BODY_VALUE 0x02

using std::string;

// Concept CollectionCompatible for objects that can be inserted into Collection:
// - Must implement member function Data() that returns a pointer
// -   Must be constructible from this pointer
// - Must have static function pointer collection_access with return type identical
//     to that of Data()
template<typename T>
concept CollectionCompatible = requires(T t, lxb_dom_collection_t* col, size_t index) {
    requires std::is_constructible_v<T, decltype(t.Data())>;
    { T::collection_access(col, index) } -> std::same_as<decltype(t.Data())>;
};

// Non-owning node wrapper
class Node
{
public:
    Node(lxb_dom_node_t* node);

    // Lexbor HTML, if asked to obtain the text content of a node, will traverse down
    // its children until text is reached if the original node is not text.
    // `deep` controls whether this behaviour is enabled or not.
    std::string_view Text(bool deep=false) const;
    Node Next() const;
    operator bool() const;

    lxb_dom_node_t* Data() const;

    constexpr static auto collection_access = lxb_dom_collection_node;
private:
    lxb_dom_node_t* ptr;
};

// Non-owning element wrapper
class Element
{
public:
    Element(lxb_dom_element_t* element);

    bool HasAttr(std::string_view attrname) const;
    std::string_view GetAttrValue(std::string_view attrname) const;

    Node FirstChild() const;

    template<typename Callable>
    Node FindChildIf(Callable&& predicate) const
    {
        Node n = FirstChild();
        while (n) {
            if (predicate(n)) return n;
            n = n.Next();
        }
        return n;
    }

    lxb_dom_element_t* Data() const;

    constexpr static auto collection_access = lxb_dom_collection_element;
private:
    lxb_dom_element_t* ptr;
};

// Special element constants
inline const Element ELEMENT_NULL { ELEMENT_NULL_VALUE };
inline const Element ELEMENT_HEAD { (lxb_dom_element_t*) ELEMENT_HEAD_VALUE };
inline const Element ELEMENT_BODY { (lxb_dom_element_t*) ELEMENT_BODY_VALUE };

// Owning collection wrapper
template<CollectionCompatible T>
class Collection
{
public:
    struct Iterator // Only implements the bare minimum required for basic for loop
    {
        Iterator() {}
        Iterator(const Collection* c, size_t i) : col(c), index(i) {}

        T operator*() const { return T::collection_access(col->Data(), index); }
        bool operator!=(const Iterator& other) const { return other.index != index; }
        void operator++() { ++index; }
    private:
        const Collection* col;
        size_t index;
    };

    Collection() : ptr(nullptr) {}
    // Merely encapsulates existing struct
    Collection(lxb_dom_collection_t* collection) : ptr(collection) {}

    // Creates new underlying object
    Collection(lxb_dom_document_t* dom, size_t capacity)
    {
        ptr = lxb_dom_collection_make(dom, capacity);
        if (!ptr && dom) Log(LogLevel::WARNING, "Failed to create collection!");
    }

    // Move constructors
    Collection(Collection&& other)
    {
        ptr = other.ptr;
        other.ptr = nullptr;
    }

    Collection& operator=(Collection&& other)
    {
        ptr = other.ptr;
        other.ptr = nullptr;
        return *this;
    }

    // Deleted copy constructors
    Collection(const Collection& other) = delete;
    Collection& operator=(const Collection& other) = delete;

    ~Collection() { if (ptr) lxb_dom_collection_destroy(ptr, true); }

    size_t size() const { return lxb_dom_collection_length(ptr); }
    lxb_dom_collection_t* Data() const { return ptr; }

    T operator[](size_t index) const { return T::collection_access(ptr, index); }

    Iterator begin() const { return { this, 0 }; }
    Iterator end() const { return { this, size() }; }

private:
    lxb_dom_collection_t* ptr;
};

class HTML
{
public:
    HTML();
    HTML(std::string_view data);

    HTML(const HTML& other) = delete; // Not CopyConstructible
    HTML& operator=(const HTML& other) = delete; // Not CopyAssignable

    ~HTML();

    void Parse(std::string_view data);

    lxb_html_document_t* Data() const;

    // Searching functions
    Collection<Element> SearchTag(std::string_view tag,
                                  Element root=ELEMENT_NULL) const;
    void SearchTag(Collection<Element>& col, std::string_view tag,
                   Element root=ELEMENT_NULL) const;

    Collection<Element> SearchAttr(std::string_view attr, std::string_view val,
                                   Element root=ELEMENT_NULL,
                                   bool broad=false) const;
    void SearchAttr(Collection<Element>& col, std::string_view attr, std::string_view val,
                    Element root=ELEMENT_NULL, bool broad=false) const;

    Collection<Element> SearchClass(std::string_view name, Element root=ELEMENT_NULL,
                                    bool broad=false) const;
    void SearchClass(Collection<Element>& col, std::string_view name,
                     Element root=ELEMENT_NULL, bool broad=false) const;

private:
    lxb_dom_element_t* Resolve(Element e) const;

    void _SearchTag(lxb_dom_collection_t* c, std::string_view tag,
                    Element root=ELEMENT_NULL) const;
    void _SearchAttr(lxb_dom_collection_t* c, std::string_view attr, std::string_view val,
                    Element root=ELEMENT_NULL, bool broad=false) const;

    lxb_html_document_t* dom;
};
