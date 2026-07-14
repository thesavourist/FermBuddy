document.addEventListener("DOMContentLoaded", () => {

		loadConnectionConfig();
		loadMainConfig();
		
		document
		.getElementById("tiltColor")
		.addEventListener(
			"change",
			loadTiltData
		);
		
		document
		.getElementById("tiltSetupForm")
		.addEventListener(
			"submit",
			saveTilt
		);
		
		document
		.querySelectorAll(
			'input[name="unit"]'
		)
		.forEach(radio =>
		{			
			radio.addEventListener(
				"change",
				refreshOGDisplay
			);
		});
		document.getElementById("savetilt").disabled = true;
		
		document
		.getElementById("doreboot")
		.addEventListener(
			"click",
			doReboot
		);

});

let currentOGSG = null;
let currentTiltSG = null;
let lastUnit;
let currentUnit = "sg";
let og;
let ogoffset;
let tiltog;

async function loadTiltData() {
	const color =
	document.getElementById(
		"tiltColor"
	).value;
	if(color != 0) {
		await loadTiltConfig();
		await loadMainConfig();
	
		lastUnit =
			document.querySelector(
				'input[name="unit"]:checked'
			).value;
			
		const ogvalue =
			parseFloat(
				document.getElementById(
					"ogstart"
				).value
			);
	
		currentOGSG =
			isNaN(ogvalue)
				? null
				: ogvalue;
		
		const tiltogvalue =
			parseFloat(
				document.getElementById(
					"tiltog"
				).value
			);
		
		currentTiltSG =
			isNaN(tiltogvalue)
				? null
				: tiltogvalue;
				
				
		if(currentOGSG === null) {
			document.getElementById("ogstart").value = "";
			return;
		}	
		else {
			og = currentOGSG;
		}	
		
		if(currentTiltSG === null) {
			document.getElementById("tiltog").value = "";
			return;
		}	
		else {
			ogoffset = currentTiltSG;
		}	
			
	
		if(lastUnit == "plato") {
			displayOG =
				-616.868 +
				1111.14 * og -
				630.272 * og * og +
				135.997 * og * og * og;
		
			document.getElementById("ogstart").value =
				displayOG.toFixed(1);
				
			
			tiltOG = 
				-616.868 +
				1111.14 * ogoffset -
				630.272 * ogoffset * ogoffset +
				135.997 * ogoffset * ogoffset * ogoffset;
			
			document.getElementById("tiltog").value =
				tiltOG.toFixed(1);		
				
		}
		else {
			document.getElementById("tiltog").value =
				tiltOG.toFixed(4);	
			document.getElementById("ogstart").value =
				og.toFixed(4);
		}		
	}	
	else {
		document.getElementById("beername").value = "";
		document.getElementById("tiltog").value = "";
		document.getElementById("ogstart").value = "";
	}
}

function refreshOGDisplay() {
	const ogfield =
		document.getElementById(
			"ogstart"
		);
		
	const tiltfield =
		document.getElementById(
			"tiltog"
		);	

	const newUnit =
		document.querySelector(
			'input[name="unit"]:checked'
		).value;

	let ogvalue =
		parseFloat(
			ogfield.value
		);
		
	let tiltvalue =
		parseFloat(
			tiltfield.value
		);	

	if (isNaN(ogvalue))
	{
		lastUnit = newUnit;
		return;
	}

	if (
		lastUnit == "plato" &&
		newUnit == "sg"
	)
	{
		// Plato -> SG

		ogvalue =
			1 +
			(
				ogvalue /
				(
					258.6 -
					(
						(ogvalue / 258.2) *
						227.1
					)
				)
			);
		
		if(!isNaN(tiltvalue)) {
			tiltvalue =
			1 +
			(
				tiltvalue /
				(
					258.6 -
					(
						(tiltvalue / 258.2) *
						227.1
					)
				)
			);
			tiltfield.value =
				tiltvalue.toFixed(4);		
		}
		else {
			tiltfield.value = "";
		}
		

		ogfield.value =
			ogvalue.toFixed(4);
			
		
	}

	if (
		lastUnit == "sg" &&
		newUnit == "plato"
	)
	{
		// SG -> Plato

		ogvalue =
			-616.868 +
			1111.14 * ogvalue -
			630.272 * ogvalue * ogvalue +
			135.997 * ogvalue * ogvalue * ogvalue;

		ogfield.value =
			ogvalue.toFixed(1);
			
		if(!isNaN(tiltvalue)) {
			tiltvalue =
				-616.868 +
				1111.14 * tiltvalue -
				630.272 * tiltvalue * tiltvalue +
				135.997 * tiltvalue * tiltvalue * tiltvalue;
			
			tiltfield.value =
				tiltvalue.toFixed(1);
		}
		else {
			tiltfield.value = "";
		}
	}

	lastUnit = newUnit;
}


async function loadTiltConfig() {
	const color =
		document.getElementById(
			"tiltColor"
		).value;
	if(color != 0) {
		document.getElementById("savetilt").disabled = false;
		const response =
			await fetch(
				"/api/hydrometerconfig?color=" +
				color
			);
	
		const data =
			await response.json();
	
		console.log(data);
	
		const og = data.ogstart;
		const tiltog = data.tiltog;
	
		document.getElementById(
			"beername"
		).value =
			data.beername;
		
		if(tiltog !== null) {
			document.getElementById("tiltog").value =
				tiltog.toFixed(4);
		}
		else {
			document.getElementById("tiltog").value = "";
		}

		if(og != "") {
			document.getElementById("ogstart").value =
					og.toFixed(4);
		}
		else {
			document.getElementById("ogstart").value = "";
		}
			
	}
	else {
		document.getElementById("savetilt").disabled = true;
	}
}

async function loadMainConfig() {
	const response =
		await fetch("/api/mainconfig");

	const data =
		await response.json();
		
	currentUnit =
		data.fermUnit == 1
			? "plato"
			: "sg";
	if (currentUnit == "sg") {
		document.getElementById("unitSG").checked = true;
	} else {
		document.getElementById("unitPlato").checked = true;
	}

}


async function loadConnectionConfig() {
	const response =
		await fetch("/api/connectionconfig");

	const data =
		await response.json();
	console.log(data.source);
	if (data.operatemode == "1" || data.operatemode == "3") {
		document.getElementById("showdetails").style.display = 'block';
	} else {
		document.getElementById("showdetails").style.display = 'none';
	}
}

async function saveTilt(event) {
	event.preventDefault();
	
	const calcOffset = document.getElementById("ogstart").value - document.getElementById("tiltog").value;
		
	const data =
	{
		tiltcol:
			document.getElementById("tiltColor").value,
			
		unit:
			document.querySelector(
				'input[name="unit"]:checked'
			).value,
	
		ogstart:
			document.getElementById("ogstart").value,
	
		beername:
			document.getElementById("beername").value,
		
		offset:
			calcOffset
	};
	console.log(data);
	
	const response = await fetch(
		"/api/hydrometersettings",
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