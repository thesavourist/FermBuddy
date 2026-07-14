document.addEventListener("DOMContentLoaded", () =>
{
	loadConnectionConfig();

	document
		.querySelectorAll('input[name="operatemode"]')
		.forEach(radio =>
		{
			radio.addEventListener(
				"change",
				updateSourceUI
			);
		});

	document
		.getElementById("wifiSetupForm")
		.addEventListener(
			"submit",
			saveSettings
		);
});

function updateSourceUI() {
	document.getElementById("wifiSettings").style.display="none";
	document.getElementById("apiSettings").style.display="none";
	
	const apiSelected =
		document.getElementById("sourceAPI").checked;
	
	const bleSelected =
		document.getElementById("sourceBLE").checked;	
	
	if(apiSelected) {
		document.getElementById("wifiSettings").style.display="block";
		document.getElementById("apiSettings").style.display="block";
	}
	else if(bleSelected) {
		document.getElementById("wifiSettings").style.display="block";
		document.getElementById("apiSettings").style.display="none";
	}

}


async function loadConnectionConfig()
{
	const response =
		await fetch("/api/setupconfig");

	const data =
		await response.json();
		console.log(data);
	document.getElementById("ssid").value =
		data.ssid;

	document.getElementById("pass").value =
		data.password;

	document.getElementById("apiurl").value =
		data.apiUrl;

	document.getElementById("hiddenwifi").checked =
		data.hiddenwifi;

	if (data.operatemode == "0") {
		document.getElementById("sourceStand").checked = true;
	} else if(data.operatemode == "1") {
		document.getElementById("sourceBLE").checked = true;
	}
	else {
		document.getElementById("sourceAPI").checked = true;
	}
	
	if (data.fermunit == "0") {
		document.getElementById("unitSG").checked = true;
	}
	else {
		document.getElementById("unitPlato").checked = true;
	}
	if (data.tempunit == "0") {
		document.getElementById("unitF").checked = true;
	}
	else {
		document.getElementById("unitC").checked = true;
	}
	document.getElementById("distimeout").value =
		data.timeout;

	updateSourceUI();
}

async function saveSettings(event)
{
	event.preventDefault();
	document.getElementById("saveconnconfig").disabled = true;
	const data =
	{
		operatemode: parseInt(
			document.querySelector(
				'input[name="operatemode"]:checked'
			).value
		),	

		ssid:
			document.getElementById("ssid").value,

		password:
			document.getElementById("pass").value,

		hiddenwifi:
			document.getElementById("hiddenwifi").checked,

		apiUrl:
			document.getElementById("apiurl").value,
		
		fermunit:
			document.querySelector(
				'input[name="fermunit"]:checked'
			).value,
		
		tempunit:
			document.querySelector(
				'input[name="tempunit"]:checked'
			).value,
		
		displaytimeout:
			document.getElementById("distimeout").value,
	};

	console.log(data);

	const response = await fetch(
		"/api/settings",
		{
			method: "POST",

			headers:
			{
				"Content-Type": "application/json"
			},

			body: JSON.stringify(data)
		}
	);
	const text =
		await response.text();

	if(text=="1") {
		setTimeout(function() {
			document.getElementById('success').style.display='block';
		}, 3000);	
	}
}