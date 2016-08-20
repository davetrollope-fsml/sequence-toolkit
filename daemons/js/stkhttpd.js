
/* state_mapping is used to allow you to override the presented name */
function state_mapping(state,state_name)
{
	if(state_name == undefined) {
		/* This is a default case when clients don't send the state name - which occurs in smartbeats.
		 * This means, if the smartbeat is received before a state change, the name is not known.
		 * It is not efficient to send state names in every smartbeat.
		 */
		if(state == 1) return "Starting";
		else if(state == 2) return "Running";
		else if(state == 3) return "Stopping";
		else if(state == 4) return "Stopped";
		else if(state == 5) return "Timed Out";
		return "" + state;
	}
	else
		return state_name; /* Add code here to customize the state name presented */
}

function svc_type_mapping(type)
{
	if(type == 1) return "I/O";
	else if(type == 2) return "Data";
	else if(type == 3) return "Management";
	else if(type == 4) return "Network";
	/* Add your custom service types here */
	return "" + type;
}

function sortSelectedColumn(column,arr)
{
	switch(column) {
	case 'name': arr.sort(function(a,b) { return a.name > b.name ? 1 : -1 } ); break;
	case 'id': arr.sort(function(a,b) { return a.id > b.id ? 1 : -1 } ); break;
	case 'checkpoint': arr.sort(function(a,b) { return a.checkpoint > b.checkpoint ? 1 : -1 } ); break;
	case 'state': arr.sort(function(a,b) { return a.state > b.state ? 1 : -1 } ); break;
	case 'type': arr.sort(function(a,b) { return a.type > b.type ? 1 : -1 } ); break;
	case 'ip': arr.sort(function(a,b) { return (a.ip + ":" + a.port) > (b.ip + ":" + b.port) ? 1 : -1 } ); break;
	case 'localtime': arr.sort(function(a,b) { return (a.tv_sec + ":" + a.tv_usec) > (b.tv_sec + ":" + b.tv_usec) ? 1 : -1 } ); break;
	case 'servertime': arr.sort(function(a,b) { return (a.rcv_tv_sec + ":" + a.rcv_tv_usec) > (b.rcv_tv_sec + ":" + b.rcv_tv_usec) ? 1 : -1 } ); break;
	}
}

var sortColumnCurrent;
var sortColumnHistory;
function sortColumn(column,arr)
{
	if(arr == services)
		sortColumnCurrent = column;
	if(arr == service_history)
		sortColumnHistory = column;
	sortSelectedColumn(column,arr);
	list_services('dservices');
}

function mk_service_table(arr,size)
{
	var n = 0,html;
	var arrname,selected_column;

	if(arr == services) {
		arrname = 'services';
		selected_column = sortColumnCurrent;
	} else {
		arrname = 'service_history';
		selected_column = sortColumnHistory;
	}

	html = "\n";
	html += "<TABLE WIDTH=100% border=1 bgcolor=#bbbbbb>";
	html += "<TR ALIGN=CENTER>\n";
	html += "<TD " + (selected_column == 'name' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('name'," + arrname + ")\">Service&nbsp;Name<BR><FONT SIZE=-1>[Group]</FONT></TD>\n";
	html += "<TD " + (selected_column == 'id' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('id'," + arrname + ")\">ID</TD>\n"
	html += "<TD " + (selected_column == 'type' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('type'," + arrname + ")\">Type</TD>\n"
	html += "<TD " + (selected_column == 'ip' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('ip'," + arrname + ")\">Protocol:IP:Port</TD>\n"
	html += "<TD " + (selected_column == 'state' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('state'," + arrname + ")\">State</TD>\n"
	html += "<TD " + (selected_column == 'checkpoint' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('checkpoint'," + arrname + ")\">Checkpoint</TD>\n"
	html += "<TD " + (selected_column == 'localtime' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('localtime'," + arrname + ")\">Activity&nbsp;Time (Service)</TD>\n"
	html += "<TD " + (selected_column == 'servertime' ? "style=\"font-weight: bold;\" " : "") + "onClick=\"sortColumn('servertime'," + arrname + ")\">Activity&nbsp;Time (Web Server)</TD></TR>\n";
	while(n < size)
	{
		var svcdate = new Date((arr[n].tv_sec*1000)+arr[n].tv_msec);
		var rcvdate = new Date((arr[n].rcv_tv_sec*1000)+arr[n].rcv_tv_msec);

		html += "<TR ALIGN=CENTER>\n"
		html += "<TD>"
		if(arr[n].name == undefined) html += "<BR>";
		else html += "<A HREF=\"/service/" + arr[n].name + "\">" + arr[n].name + "</A>";
		if(arr[n].group_name == undefined) html += "<BR>";
		else html += "<BR><FONT SIZE=-1>[<A HREF=\"/group/" + arr[n].group_name + "\">" + arr[n].group_name + "</A>]</FONT>";
		html += "</TD>\n";
		html += "<TD><A HREF=\"/service_id/" + arr[n].hid + "\">" + arr[n].id + " (" + arr[n].hid + ")</A> </TD><TD>" + svc_type_mapping(arr[n].type) + "</TD>\n";
		html += "<TD>" + arr[n].protocol + ":<A HREF=\"/service_ip/" + arr[n].ip + "\">" + arr[n].ip + "</A>:" + arr[n].port + "</TD>\n<TD>";

		statename = state_mapping(arr[n].state,arr[n].state_name);

		html += (arr[n].displaced != undefined ? 
				" <FONT COLOR=red>[displaced]</FONT><BR><FONT SIZE=-2>(was&nbsp;" + statename + ")</FONT>" :
				(arr[n].inactivity != undefined ? 
					" <FONT COLOR=red>[inactive]</FONT><BR><FONT SIZE=-2>(was&nbsp;" + statename + ")</FONT>" : statename) ) +
			"</TD><TD>" + arr[n].checkpoint + "</TD><TD>" + svcdate.toLocaleString() + "</TD><TD>" + rcvdate.toLocaleString() + "</TD></TR>\n";
		n++;
	}
	html += "</TABLE>\n";
	return html;
}

function list_services(elem)
{
	var html = "";
	if(typeof daemon_obj == "undefined") return;

	if(typeof daemon_obj.sec != "undefined") {
		var daemondate = new Date((daemon_obj.sec*1000)+daemon_obj.msec);
		document.getElementById("generated_date").innerHTML=daemondate.toLocaleString();
	}
	html += "<TABLE WIDTH=100%><TR><TD><H2>Current Services</H2></TD>\n";
	html += "<TD VALIGN=bottom ALIGN=RIGHT>(" + services_size + ")</TD></TR></TABLE>\n";

	/* Generate the chart and copy the HTML from the parent div which is hidden */
	generate_graph_data(services);
	generate_graph("chart");
	html += '<div id="live_chart" class="chart">';
	html += document.getElementById("chart").innerHTML;
	html += '</div>';

	html += mk_service_table(services,services_size);
	html += "<TABLE WIDTH=100%><TR><TD><H2>Service History</H2></TD>\n";
	html += "<TD VALIGN=bottom ALIGN=RIGHT>(" + services_history_size + ")</TD></TR></TABLE>\n";
	generate_graph_data(service_history);
	generate_graph("history_chart");
	html += '<div id="history_live_chart" class="chart">';
	html += document.getElementById("history_chart").innerHTML;
	html += '</div>';

	html += mk_service_table(service_history,services_history_size);

	document.getElementById(elem).innerHTML = html;
}

function present_services(elem)
{
	if(typeof sortColumnCurrent != "undefined")
		sortColumn(sortColumnCurrent,services);
	if(typeof sortColumnHistory != "undefined")
		sortColumn(sortColumnHistory,service_history);
	list_services(elem);
}

var stkloaded = true;
var childloaded = false;
var services,services_size,svc_obj,daemon_obj,service_history,services_history_size;

var gobjects = []; /* Objects being created for graphing purposes */

function generate_graph_data_array(arr)
{
	var n = 0;

	while(n < arr.length) {
		statename = state_mapping(arr[n].state,arr[n].state_name);
		if(arr[n].inactivity != undefined) statename += "(inactive)";
		i = 0;
		while(i < gobjects.length) {
			if(statename == gobjects[i].statename)
				break;
			i = i + 1;
		}
		if(i >= gobjects.length) {
			x = gobjects.length;
			gobjects[x] = new Object();
			gobjects[x].statename = statename;
			gobjects[x].data = 1;
		} else
			gobjects[i].data++;
		n = n + 1;
	}
}

function generate_graph_data(arr)
{
	gobjects=[];
	generate_graph_data_array(arr);
	gobjects.sort(function(a,b) { return a.statename > b.statename ? 1 : -1 } );
}

