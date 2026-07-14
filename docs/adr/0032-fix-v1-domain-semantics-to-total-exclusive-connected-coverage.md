# Fix V1 Domain semantics to total, exclusive, connected coverage

For every Package-declared V1 Domain type, every Router belongs to exactly one Domain of that type; membership is total and exclusive, and each Domain is connected in the undirected graph of current structural Links. A non-deletable Default Domain initially and automatically receives all Routers, single-Router Domains are connected, and changes occur only through atomic `MoveRoutersBetweenDomains`, `SplitDomain`, or `MergeDomains` commands that leave every affected Domain valid.
