
var chart;
var x, y, width = 400, bar_height = 20, height;

function generate_graph(id)
{
	var left_width = 150;

	var names=[];
	var data=[];
	for(n = 0; n < gobjects.length; n++) {
		names[n] = gobjects[n].statename;
		data[n] = gobjects[n].data;
	}

	if(document.getElementById(id) == null) return;
	document.getElementById(id).innerHTML = "";
	if(names.length == 0) return;

	height = bar_height * names.length;
	d3 = parent.d3
	x = d3.scale.linear()
	   .domain([0, d3.max(data)])
	   .range([0, width]);

	y = d3.scale.ordinal()
	   .domain(data)
	   .rangeBands([0, height]);

	chart = d3.select("#" + id)
	  .append('svg')
	  .attr('class', 'chart')
	  .attr('width', left_width + width)
	  .attr('height', height);

	chart.selectAll("rect")
	  .data(data)
	  .enter().append("rect")
	  .attr("x", left_width)
	  .attr("y", y)
	  .attr("width", x)
	  .attr("height", y.rangeBand());

	chart.selectAll("text.score")
	  .data(data)
	  .enter().append("text")
	  .attr("x", function(d) { return x(d) + left_width; })
	  .attr("y", function(d){ return y(d) + y.rangeBand()/2; } )
	  .attr("dx", -5)
	  .attr("dy", ".36em")
	  .attr("text-anchor", "end")
	  .attr('class', 'score')
	  .text(String);

	chart.selectAll("text.name")
	  .data(names)
	  .enter().append("text")
	  .attr("x", left_width / 2)
	  //   .attr("y", function(d){ return y(d) + y.rangeBand()/2; } )
	  .attr("y", function(d,i) {return (i * y.rangeBand()) + (y.rangeBand()/2); })
	  .attr("dy", ".36em")
	  .attr("text-anchor", "middle")
	  .attr('class', 'name')
	  .text(String);
}

function ogenerate_graph()
{
if(document.getElementById("chart") == null) return;
d3 = parent.d3
d3.select(".chart")
  .selectAll("div")
    .data(data)
  .enter().append("div")
    .style("width", function(d) { return x(d) + "px"; })
    .text(function(d) { return d; });
}
