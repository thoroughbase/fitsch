#include "webscraper/html.hpp"

#include "common/util.hpp"

// Node

Node::Node(lxb_dom_node_t* node) : ptr(node) {}

std::string_view Node::Text(bool deep) const
{
    if (!ptr || (ptr->type != LXB_DOM_NODE_TYPE_TEXT && !deep)) return {};
    return reinterpret_cast<const char*>(lxb_dom_node_text_content(ptr, nullptr));
}

Node Node::Next() const { return lxb_dom_node_next(ptr); }

Node::operator bool() const { return ptr; }

lxb_dom_node_t* Node::Data() const { return ptr; }

// Element

Element::Element(lxb_dom_element_t* element) : ptr(element) {}

Element::Element(DOMTag dom_tag) : dom_tag(dom_tag) {}

bool Element::HasAttr(std::string_view attrname) const
{
    return lxb_dom_element_attr_is_exist(ptr,
        reinterpret_cast<const lxb_char_t*>(attrname.data()), attrname.size());
}

std::string_view Element::GetAttrValue(std::string_view attrname) const
{
    lxb_dom_attr_t* attr;
    if (!(attr = lxb_dom_element_attr_by_name(ptr,
            reinterpret_cast<const lxb_char_t*>(attrname.data()),
            attrname.size()))) {
        Log(LogLevel::WARNING, "Attribute {} not found!", attrname);
        return {};
    }

    return reinterpret_cast<const char*>(lxb_dom_attr_value(attr, nullptr));
}

Node Element::FirstChild() const
{
    return lxb_dom_node_first_child(lxb_dom_interface_node(ptr));
}

lxb_dom_element_t* Element::Data() const { return ptr; }

// HTML

HTML::HTML()
{
    dom = lxb_html_document_create();
    if (!dom) Log(LogLevel::WARNING, "Error creating DOM");
}

HTML::HTML(std::string_view data) : HTML() { Parse(data); }

HTML::~HTML() { lxb_html_document_destroy(dom); }

void HTML::Parse(std::string_view data)
{
    lxb_status_t status =
        lxb_html_document_parse(dom,
            reinterpret_cast<const lxb_char_t*>(data.data()), data.size());

    if (status != LXB_STATUS_OK) {
        Log(LogLevel::WARNING, "Reading page failed with status: {}", status);
    }
}

lxb_html_document_t* HTML::Data() const { return dom; }

// HTML searching functions

Collection<Element> HTML::SearchTag(std::string_view tag,
                              Element root) const
{
    Collection<Element> col(&(dom->dom_document), 16);
    _SearchTag(col.Data(), tag, root);

    return col;
}

void HTML::SearchTag(Collection<Element>& col, std::string_view tag,
               Element root) const
{
    if (!col.Data()) col = Collection<Element>(&(dom->dom_document), 16);
    _SearchTag(col.Data(), tag, root);
}

void HTML::_SearchTag(lxb_dom_collection_t* c, std::string_view tag,
                    Element root) const
{
    lxb_dom_element_t* rootptr = Resolve(root);
    lxb_dom_collection_clean(c);

    lxb_status_t status
        = lxb_dom_elements_by_tag_name(rootptr, c, (lxb_char_t*)tag.data(), tag.size());

    if (status != LXB_STATUS_OK)
        Log(LogLevel::WARNING, "SearchTag failed with code {}", status);
}

Collection<Element> HTML::SearchAttr(std::string_view attr, std::string_view val,
                               Element root,
                               bool broad) const
{
    Collection<Element> col(&(dom->dom_document), 16);
    _SearchAttr(col.Data(), attr, val, root, broad);

    return col;
}

void HTML::SearchAttr(Collection<Element>& col, std::string_view attr,
                std::string_view val, Element root, bool broad) const
{
    if (!col.Data()) col = Collection<Element>(&(dom->dom_document), 16);
    _SearchAttr(col.Data(), attr, val, root, broad);
}

Collection<Element> HTML::SearchClass(std::string_view name, Element root,
                                bool broad) const
{
    Collection<Element> col(&(dom->dom_document), 16);
    _SearchAttr(col.Data(), "class", name, root, broad);

    return col;
}

void HTML::SearchClass(Collection<Element>& col, std::string_view name,
                 Element root, bool broad) const
{
    if (!col.Data()) col = Collection<Element>(&(dom->dom_document), 16);
    _SearchAttr(col.Data(), "class", name, root, broad);
}

lxb_dom_element_t* HTML::Resolve(Element e) const
{
    switch (e.dom_tag) {
    case Element::ROOT: return lxb_dom_interface_element(dom);
    case Element::HEAD: return lxb_dom_interface_element(dom->head);
    case Element::BODY: return lxb_dom_interface_element(dom->body);
    default: return e.Data();
    }
}

void HTML::_SearchAttr(lxb_dom_collection_t* c, std::string_view attr,
                  std::string_view val, Element root, bool broad) const
{
    lxb_dom_element_t* rootptr = Resolve(root);
    lxb_dom_collection_clean(c);

    auto func = broad ? lxb_dom_elements_by_attr_contain : lxb_dom_elements_by_attr;

    lxb_status_t status
        = func(rootptr, c, reinterpret_cast<const lxb_char_t*>(attr.data()),
               attr.size(), reinterpret_cast<const lxb_char_t*>(val.data()),
               val.size(), broad);

    if (status != LXB_STATUS_OK)
        Log(LogLevel::WARNING, "SearchAttr failed with code {}", status);
}
