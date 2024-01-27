// --------------------------------------------------------------------------
//   From the content of style.json build the controls to change rendering
// --------------------------------------------------------------------------
function parse_style_json( style )
{
    console.log("parse_style_json");

    for ( const layer of style.layers ) {
	const lid = layer.id;

	console.log( "Layer " + lid);
	
	$("<h3>", {
	    "id":    lid,
	    "class": "layer"
	}).appendTo("#layer-list");
	$("#" + lid).text( lid );

	$("<div>", {
	    "id":    "div-" + lid
	}).appendTo("#layer-list");

	// -- Checkbutton group
	$("<fieldset>", {
	    "id": "fset-vis-" + lid
	}).appendTo("#div-" + lid);
	$("<legend>", {
	    "id": "fset-vis-legend-" + lid
	}).appendTo("#fset-vis-" + lid);
	$("#fset-vis-legend-" + lid).text("Visibility");

	// --------------------------------------------------
	//   Checkbutton for setting visibility
	// --------------------------------------------------
	$("<label>", {
	    "id":   "label-vis-" + lid,
	    "for":   "vis-" + lid,
	}).appendTo("#fset-vis-" + lid);
	$("#label-vis-" + lid).text( "visible" );

	$("<input>", {
	    "id":    "vis-" + lid,
	    "name":  "vis-" + lid,
	    "type":  "checkbox",
	    "class": "styled"
	}).appendTo("#fset-vis-" + lid);
	console.log("Layer " + lid + " visibility " + map.getLayoutProperty(lid, 'visibility'));
	$("#vis-" + lid).prop("checked",true);
	$("#vis-" + lid).click( function() {
	    console.log("Toggle layer visibility " + lid );
	    if ( $("#vis-" + lid).is(":checked") ) {
		map.setLayoutProperty(lid, 'visibility', 'visible');
	    }
	    else {
		map.setLayoutProperty(lid, 'visibility', 'none');
	    }
	});
	// --------------------------------------------------

	if (!lid.endsWith('-fill') && !lid.endsWith('-line')) {
	    // -- This will be a radio button group
	    $("<fieldset>", {
		"id": "fset-mode-" + lid
	    }).appendTo("#div-" + lid);
	    $("<legend>", {
		"id": "fset-mode-legend-" + lid
	    }).appendTo("#fset-mode-" + lid);
	    $("#fset-mode-legend-" + lid).text("rendering mode");
	    
	    // --------------------------------------------------
	    // -- Radio button for line rendering
	    // --------------------------------------------------
	    $("<input>", {
		"id":   "line-" + lid,
		"name":   "render-" + lid,
		"type": "radio",
		"class": "styled"
	    }).appendTo("#fset-mode-" + lid);

	    $("<label>", {
		"id":   "label-line-" + lid,
		"for":   "line-" + lid,
	    }).appendTo("#fset-mode-" + lid);
	    $("#label-line-" + lid).text( "line" );
	    // --------------------------------------------------

	    // --------------------------------------------------
	    // -- Radio button for point rendering
	    // --------------------------------------------------
	    $("<input>", {
		"id":   "point-" + lid,
		"name":   "render-" + lid,
		"type": "radio",
		"class": "styled"
	    }).appendTo("#fset-mode-" + lid);

	    $("<label>", {
		"id":   "label-point-" + lid,
		"for":   "point-" + lid,
	    }).appendTo("#fset-mode-" + lid);
	    $("#label-point-" + lid).text( "point" );
	    // --------------------------------------------------

	    // --------------------------------------------------
	    // -- Radio button for fill rendering
	    // --------------------------------------------------
	    $("<input>", {
		"id":   "fill-" + lid,
		"name":   "render-" + lid,
		"type": "radio",
		"class": "styled"
	    }).appendTo("#fset-mode-" + lid);

	    $("<label>", {
		"id":   "label-fill-" + lid,
		"for":   "fill-" + lid,
	    }).appendTo("#fset-mode-" + lid);
	    $("#label-fill-" + lid).text( "fill" );
	    // --------------------------------------------------
	}

	
	// -- color group
	$("<fieldset>", {
	    "id": "fset-color-" + lid
	}).appendTo("#div-" + lid);
	$("<legend>", {
	    "id": "fset-color-legend-" + lid
	}).appendTo("#fset-color-" + lid);
	$("#fset-color-legend-" + lid).text("Colour");

	// --------------------------------------------------
	// -- Rendering color chooser
	// --------------------------------------------------
	$("<input>", {
	    "id":   "color-" + lid,
	    "name": "color-" + lid,
	    "type": "color"
	}).appendTo("#fset-color-" + lid);

	$("<label>", {
	    "id":   "label-color-" + lid,
	    "for":   "color-" + lid,
	}).appendTo("#fset-color-" + lid);
	$("#label-color-" + lid).text( "Color" );
	$("#color-" + lid).change( function() {
	    console.log("color change layer " + lid);
	    if (lid.endsWith('-fill')) {
		map.setPaintProperty( lid, "fill-color", $(this).val());
	    }
	    else if (lid.endsWith('-line')) {
		map.setPaintProperty( lid, "line-color", $(this).val());
	    }
	    else {
		map.setPaintProperty( lid, "fill-color", $(this).val());
	    }
	});
	// --------------------------------------------------
    }
    
    $( "#layer-list" ).accordion();
    $( ".styled" ).checkboxradio();
}

