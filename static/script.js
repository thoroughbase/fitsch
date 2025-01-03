const search_bar = document.getElementById("search-field");
const search_button = document.getElementById("search-button");

search_bar.addEventListener("keyup", (event) => {
    if (event.key !== "Enter") return;
    search_button.click();
    event.preventDefault();
});

function search()
{
    const term = encodeURIComponent(search_bar.value.trim());
    if (term.length == 0) return;
    window.location = "/search/" + term;
}
