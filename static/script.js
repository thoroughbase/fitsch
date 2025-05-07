const search_bar = document.getElementById("search-field");
const search_button = document.getElementById("search-button");
const search_button_div = document.getElementById("search-button-div");
const search_bar_div = document.getElementById("search-field-div");
const header_title = document.getElementById("header-title");
const MOBILE_VIEWPORT_WIDTH_THRESHOLD = 500;
const listings_element = document.getElementById("listings");
const original_listings
    = (listings_element === null) ? [] : [...listings_element.children];
const item_count = document.getElementById("item-count");

search_bar.addEventListener("keyup", (event) => {
    if (event.key !== "Enter") return;
    search();
    event.preventDefault();
});

search_button.addEventListener("click", (event) => {
    focus_search_bar_small();
});

search_bar.addEventListener("blur", (event) => {
    unfocus_search_bar_small();
});

window.addEventListener("resize", (event) => {
    update_header();
});

function filter_products_by_store(stores)
{
    const new_listings = original_listings.filter((elem) => {
        return stores.includes(elem.getAttribute("store-id"))
    });

    listings_element.replaceChildren(...new_listings);
    item_count.innerText = listings_element.children.length + " results";
}

function update_header()
{
    if (window.innerWidth < MOBILE_VIEWPORT_WIDTH_THRESHOLD) {
        if (document.activeElement !== search_bar) {
            search_bar_div.style.visibility = "hidden";
            search_button_div.style.visibility = "visible";
            header_title.style.visibility = "visible";
        } else {
            search_bar_div.style.visibility = "visible";
            search_button_div.style.visibility = "hidden";
            header_title.style.visibility = "hidden";
        }
    } else {
        header_title.style.visibility = "visible";
        search_button_div.style.visibility = "hidden";
        search_bar_div.style.visibility = "visible";
    }
}

function search()
{
    const term = encodeURIComponent(search_bar.value.trim());
    if (term.length == 0) return;
    window.location = "/search/" + term;
}

function focus_search_bar_small()
{
    search_bar_div.style.visibility = "visible";
    search_bar.focus();
    if (window.innerWidth < MOBILE_VIEWPORT_WIDTH_THRESHOLD) {
        search_button_div.style.visibility = "hidden";
        header_title.style.visibility = "hidden";
    }
}

function unfocus_search_bar_small()
{
    update_header();
}
