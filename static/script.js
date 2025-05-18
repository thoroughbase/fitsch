const search_bar = document.getElementById("search-field");
const search_button = document.getElementById("search-button");
const home_button = document.getElementById("home-button");
const buttons_div = document.getElementById("buttons-div");
const search_bar_div = document.getElementById("search-field-div");
const header_title = document.getElementById("header-title");
const MOBILE_VIEWPORT_WIDTH_THRESHOLD = 500;
const listings_element = document.getElementById("listings");
const original_listings
    = (listings_element === null) ? [] : [...listings_element.children];
const item_count = document.getElementById("item-count");
const overlay = document.getElementById("overlay");
const tooltips = document.getElementsByClassName("tooltip");

search_bar.addEventListener("keyup", (event) => {
    if (event.key !== "Enter") return;
    search();
    event.preventDefault();
});

search_button.addEventListener("click", (event) => {
    focus_search_bar_small();
});

home_button.addEventListener("click", (event) => {
    window.location = "/";
})

search_bar.addEventListener("blur", (event) => {
    unfocus_search_bar_small();
    overlay.style.opacity = 0;
});

window.addEventListener("resize", (event) => {
    update_header();
});

search_bar.addEventListener("focus", (event) => {
    overlay.style.opacity = 0.15;
});

update_header();
center_tooltips();

function center_tooltips()
{
    for (tooltip of tooltips) {
        tooltip.style.left = "-" + (tooltip.offsetWidth / 2 - 11) + "px";
    }
}

function filter_products_by_store(stores)
{
    const new_listings = original_listings.filter((elem) => {
        return stores.includes(elem.getAttribute("store-id"))
    });

    listings_element.replaceChildren(...new_listings);
    item_count.innerText = listings_element.children.length + " results";
}

function show_elements(elements)
{
    for (element of elements) {
        element.style.visibility = "visible";
    }
}

function hide_elements(elements)
{
    for (element of elements) {
        element.style.visibility = "hidden";
    }
}

function update_header()
{
    if (window.innerWidth < MOBILE_VIEWPORT_WIDTH_THRESHOLD) {
        if (document.activeElement !== search_bar) {
            hide_elements([search_bar_div]);
            show_elements([buttons_div, header_title, search_button]);
        } else {
            show_elements([search_bar_div]);
            hide_elements([search_button, buttons_div, header_title]);
        }
    } else {
        show_elements([header_title, search_bar_div, buttons_div]);
        hide_elements([search_button]);
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
    show_elements([search_bar_div]); // Required for focus to work
    search_bar.focus();
    update_header();
}

function unfocus_search_bar_small()
{
    update_header();
}
