# flight-logging-simulator
A small networking &amp; multi-threading project which simulates communications between aircraft and control towers.

## Mapper (mapper2310.c)
### Args: none
### Description
Used by control and roc to map airport IDs to their associated port number.
Upon start-up, listens on an ephemeral port and prints that port to stdout. This port may be used as an argument for control and roc.
Can process multiple requests in parallel.
Returns a list of all registrations if sent "@".
Returns the associated port number of an id if sent "?*ID*".

## Control (control2310.c)
### Args: id info [mapper]
- id: the ID of this airport, e.g. 'Brisbane'.
- info: information associated with this airport, e.g. 'Quarantined due to coronavirus'.
- [mapper]: (optional) port number of a mapper.
### Description
Represents an airport control tower. Is "visited by aircraft" (i.e. connected to by roc processes).
Upon start-up, listens on an ephemeral port and prints that port to stdout. This port may be used as an argument for roc. Then, registers its ID and port number with the given mapper, if one exists.
In parallel, waits for connections by aircraft and acts on them.
If the control receives the text "log" by the connecting party, it prints a log of all rocs which have visited them in lexicographic order, followed by a full stop, then exits.
Control registers all other received text as roc IDs, and stores them in the aforementioned log.

## Roc (roc2310.c)
### Args: id mapper {airports}
- id: the ID of this aircraft, e.g. 'Virgin747'.
- mapper: port number of a mapper, or '-' if not using a mapper.
- {airports}: list of airport controls (as IDs or port numbers) for this aircraft to visit in turn.
### Description
Represents an aircraft.
Upon start-up, requests the port numbers for all given airport control IDs from the mapper.
Then, visits (connects to) each given airport in turn, adding that airport's associated information to its log.
Once all airports have been visited, prints its log to stdout.

## Example Usage
Commands to be run in separate terminal tabs.

    > mapper2310
    55000
    > control2310 brisbane quarantined 55000
    55001
    > control2310 perth out_west 55000
    55002
    > roc2310 F100 55000 perth 55001 55000
    out_west
    quarantined
    out_west
