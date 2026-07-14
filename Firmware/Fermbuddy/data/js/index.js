document.addEventListener("DOMContentLoaded", () =>
{
	loadDashboard();
	
	document
	.getElementById("doreboot")
	.addEventListener(
		"click",
		doReboot
	);

});

async function loadDashboard() {

	const response =
		await fetch(
			"/api/dashboard"
		);

	const tilts =
		await response.json();

	renderDashboard(
		tilts
	);
}


function renderDashboard(tilts) {

	let html = "";

	tilts.forEach(
		tilt => {

			html += createCard(
				tilt
			);

		}
	);
	document.getElementById(
		"tiltContainer"
	).innerHTML = html;
}

function createCard(tilt) {

	return `
		<div class="tilt-card">

			<div class="tilt-name">
				${tilt.beername}
			</div>
			<div class="tilt-cloudurl">${tilt.cloudUrl ?? ""}</div>
			<div
				class="tilt-color-bar"
				style="
					background:
					${getTiltColor(
						tilt.color
					)};
				"
			></div>

			<div class="tilt-metric">
				<span>OG</span>
				<span>
					${
						tilt.og > 0
						? tilt.og
						: "-"
					} ${tilt.gravityUnit}
				</span>
			</div>

			<div class="tilt-metric current">
				<span>Current</span>
				<span>
					${tilt.gravity} ${tilt.gravityUnit}
				</span>
			</div>

			<div class="tilt-metric">
				<span>Temperature</span>
				<span>
					${parseFloat(
						tilt.temp
					).toFixed(1)} ${tilt.tempUnit}
				</span>
			</div>
			
			<div class="tilt-metric">
				  <span>Attenuation</span>
				  <span>${parseFloat(
					  tilt.attenuation
				  ).toFixed(1)} %</span>
			  </div>
			
			  <div class="tilt-metric">
				  <span>ABV</span>
				  <span>${parseFloat(
						tilt.abv
					).toFixed(1)} %</span>
			  </div>

		</div>
	`;
}

function getTiltColor(color) {

	switch(
		color.toUpperCase()
	) {

		case "RED":
			return "#cc0000";

		case "GREEN":
			return "#00aa00";

		case "BLUE":
			return "#0066ff";

		case "BLACK":
			return "#111111";

		case "PURPLE":
			return "purple";

		case "ORANGE":
			return "orange";

		case "YELLOW":
			return "gold";

		case "PINK":
			return "hotpink";

		default:
			return "#888";
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