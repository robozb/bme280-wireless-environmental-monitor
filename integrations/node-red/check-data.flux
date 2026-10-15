// InfluxDB Data Explorer: AksisBME, recent numeric/boolean and text data.
// The separate `type` tag keeps each source's fields easy to query.
from(bucket: "AksisBME")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "aksis_bme")
  |> last()
