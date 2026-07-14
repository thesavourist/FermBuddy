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
	
	document
	.getElementById("doreboot")
	.addEventListener(
		"click",
		doReboot
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
		await fetch("/api/connectionconfig");

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

	if (data.operatemode == "0" || data.operatemode == "3") {
		document.getElementById("sourceStand").checked = true;
	} else if(data.operatemode == "1") {
		document.getElementById("sourceBLE").checked = true;
	}
	else {
		document.getElementById("sourceAPI").checked = true;
	}

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
			document.getElementById("apiurl").value
	};

	console.log(data);

	const response = await fetch(
		"/api/connectionsave",
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
async function doReboot(event) {
	event.preventDefault();
	ask = confirm("Do you want to reboot FermBuddy?");
	if(ask) {
		const data =
		{
			reboot:"1"
		};
			const response = await fetch(
				"/api/reboot",
				{
					method: "POST",
			
					headers:
					{
						"Content-Type": "application/json"
					},
			
					body: JSON.stringify(data)
				}
			);

	}
}