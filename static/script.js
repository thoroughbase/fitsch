const search_bar = document.getElementById("search-field");
const search_button = document.getElementById("search-button");
const search_button_div = document.getElementById("search-button-div");
const search_bar_div = document.getElementById("search-field-div");
const header_title = document.getElementById("header-title");

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
    if (window.innerWidth < 500 && document.activeElement !== search_bar) {
        search_bar_div.style.visibility = "hidden";
        search_button_div.style.visibility = "visible";
    } else {
        search_bar_div.style.visibility = "visible";
        search_button_div.style.visibility = "hidden";
    }
});

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
    if (window.innerWidth < 500) {
        search_button_div.style.visibility = "hidden";
        header_title.style.visibility = "hidden";
    }
}

function unfocus_search_bar_small()
{
    if (window.innerWidth < 500 && document.activeElement !== search_bar) {
        search_bar_div.style.visibility = "hidden";
        search_button_div.style.visibility = "visible";
    } else {
        search_bar_div.style.visibility = "visible";
        search_button_div.style.visibility = "hidden";
    }

    header_title.style.visibility = "visible";
}
