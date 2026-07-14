document.addEventListener("DOMContentLoaded", () => {

		loadMainConfig();
		document
		.getElementById("resettilts")
		.addEventListener(
			"click",
			resetTilts
		);
		
		document
		.getElementById("factoryreset")
		.addEventListener(
			"click",
			factoryReset
		);
		document
		.getElementById("settingsForm")
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

async function loadMainConfig() {
	const response =
		await fetch("/api/mainconfig");

	const data =
		await response.json();
	if (data.fermUnit == 0) {
		document.getElementById("unitSG").checked = true;
	} else {
		document.getElementById("unitPlato").checked = true;
	}

	if (data.tempUnit == 0) {
		document.getElementById("unitF").checked = true;
	} else {
		document.getElementById("unitC").checked = true;
	}
	
	if (data.screenOrientation == 1) {
		document.getElementById("orientation1").selected = true;
	}
	else {
		document.getElementById("orientation3").selected = true;
	}
	
	document.getElementById(
		"distimeout"
	).value =
		data.displaytimeout;

}

async function saveSettings(event) {
	event.preventDefault();
	const data =
	{
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

		screenOrientation:
			document.getElementById("orientation").value
	};

	console.log(data);

	const response = await fetch(
		"/api/savemainconfig",
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
		const success =
			document.getElementById(
				"success"
			);
		
		success.classList.remove(
			"d-none"
		);
		
		setTimeout(
			() =>
			{
				success.classList.add(
					"d-none"
				);
			},
			3000
		);
	}
}
async function resetTilts(event) {
	event.preventDefault();
	ask = confirm("Do you want to clear all Hydrometer data?");
	if(ask) {
		const data =
		{
			clearTilts:"1"
		};
	
		console.log(data);
	
		const response = await fetch(
			"/api/resetdata",
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
			const success =
				document.getElementById(
					"clearsuccess"
				);
			
			success.classList.remove(
				"d-none"
			);
			
			setTimeout(
				() =>
				{
					success.classList.add(
						"d-none"
					);
				},
				3000
			);
		}
	}
}
async function factoryReset(event) {
	event.preventDefault();
	ask = confirm("Do you want to set FermBuddy to factory settings?");
	if(ask) {
		const data =
		{
			clearAll:"1"
		};
	
		console.log(data);
	
		const response = await fetch(
			"/api/resetdata",
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
	
		if(text=="2") {
			const success =
				document.getElementById(
					"clearsuccess"
				);
			
			success.classList.remove(
				"d-none"
			);
			
			setTimeout(
				() =>
				{
					success.classList.add(
						"d-none"
					);
				},
				3000
			);
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