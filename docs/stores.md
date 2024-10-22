# Shop Website Format

This document describes the organisation of product information within webpages retrieved by fitsch. As of writing, the following fields are of interest:

* Name of the product
* Product description
* Product page URL
* Product image URL
* Unique identifier as assigned by the website
* Price of the item
* Price per unit (weight, volume etc.)

This is reflected in the definition of the `Product` struct:

```cpp
struct Product
{
	string name, description, image_url, url, id;
	Price item_price;
	PricePU price_per_unit; // Price per KG, L, etc.
	StoreID store;
	std::time_t timestamp;

	bool full_info;
};
```

## SuperValu

Last updated: 10th October 2024

### Single product page

[Sample page](https://shop.supervalu.ie/sm/delivery/rsid/5550/product/supervalu-chick-peas-400-g-id-1404574000)

All properties except the price per unit can be found in `<meta>` tags in the `<head>` section.
The name of the property is contained in the `itemprop` attribute, and the value in the `content` attribute, or in `href` in the case of the `image` property.

Example:
```html
<meta itemprop="name" content="SuperValu Chick Peas (400 g)">
<meta itemprop="description" content="SuperValu Chick Peas 400g">
<meta itemprop="image" href="https://images.cdn.shop.supervalu.ie/detail/1404574000_1">
<meta itemprop="price" content="€0.44">
<meta itemprop="sku" content="1404574000">
```

Property | `itemprop` value | Value attribute name
---|---|---
Name|`name`|`content`
Description|`description`|`content`
Image URL|`image`|`href`
Price|`price`|`content`
Unique Identifier|`sku`|`content`

The price per unit is contained in a `<span>` element whose class begins with `PdpUnitPrice`:

```html
<span data-testid="pdpUnitPrice-div-testId"
      class="PdpUnitPrice--1d8aj6w bIOJSc">
  €1.10/kg
</span>
```

This information may not be present for all items.

### Search results page

[Sample page](https://shop.supervalu.ie/sm/delivery/rsid/5550/results?q=chickpeas)

SuperValu by default displays up to 30 items per page. Each item is contained in a `<div>` of class `ColListing`, with the child element `<article>` of class `ProductWrapper` encapsulating all relevant information.

```html
<div class="ColListing--1fk1zey jBeiE">
  <article class="ProductCardWrapper--6uxd5a gIbeIw" ...>
    <!-- (1) -->
    <a href="https://shop.supervalu.ie/sm/delivery/rsid/5550/product/supervalu-chick-peas-400-g-id-1404574000"
       class="ProductCardHiddenLink--v3c62m gQTnmz"></a>
    <!-- (2) -->
    <div class="ProductCardImageWrapper--klzjiv feMvRj">
      <div data-testid="productCardImage_1404574000-testId" ...>
         <img class="Image--v39pjb kRhNlo ProductCardImage--qpr2ve cLdMub"
              src="https://images.cdn.shop.supervalu.ie/detail/1404574000_1" ...>
      </div>
    </div>
    <!-- (3) & (4) -->
    <span class="ProductCardTitle--1ln1u3g fgbJDn">
      <div data-testid="1404574000-ProductNameTestId" ...>
        SuperValu Chick Peas (400 g)
        ...
      </div>
    </span>
    <!-- (5) & (6) -->
    <div class="ProductCardPricing--t1f7no iDlwSZ" ...>
      <span>
        <span class="ProductCardPrice--xq2y7a fHLbbx">€0.44</span>
      </span>
      <span class="ProductCardPriceInfo--1vvb8df jDXhAF">€1.10/kg</span>
    </div>
  </article>
</div>
```
*Extraneous information not pertinent to this document has been omitted using ellipses.*

(1) The product URL is contained in an `<a>` element, a direct child of the `<article>` element.
(2) The image URL is contained in an `<img>` element within the `<div>` of class `ProductCardImageWrapper`
(3) The product name is contained as a text element in the `<div>` whose attribute `data-testid` contains `ProductNameTestId`.
(4) The product ID is contained in the value of the aforementioned attribute `data-testid` left of the hyphen.
(5) The item price is contained in a `<span>` element of class `ProductCardPrice`.
(6) The price per unit is contained in a `<span>` element of class `ProductCardPriceInfo`. This information may not be present for some items.
