// SPDX-License-Identifier: GPL-3.0-or-later
// Aircraft-local scenery boundary. No HTTP, demo airports or Navigraph credentials.
export class NavigraphAmdbClient {
  async searchForAirports(query) {
    const needle = query.toUpperCase();
    return (window.__a380Nd?.state?.nav?.airports || []).filter((a) => (a.idarpt+' '+a.name).toUpperCase().includes(needle));
  }
  async getAirportData(icao, include, exclude, projection = 'NAVIGRAPH:ARP_AZEQ') {
    const airport = await window.__a380Nd.airport(icao);
    const data = projection === 'EPSG:4326' ? airport.wgs84 : airport.metric;
    return Object.fromEntries(Object.entries(data).filter(([key]) => (!include || include.includes(key)) && (!exclude || !exclude.includes(key))));
  }
}
