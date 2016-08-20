
function copy_to_parent()
{
	if(typeof parent.parent.present_services != "undefined") {
		parent.parent.services = services;
		parent.parent.services_size = services_size;
		parent.parent.service_history = service_history;
		parent.parent.services_history_size = services_history_size;
		parent.parent.daemon_obj = daemon_obj;
		parent.parent.present_services('dservices');
	}
}

