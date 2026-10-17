-- A weighted ground-grid separator: impassable terrain/objects need no walls.
-- The protected rectangle reserves base space; finite cuts may be wider than a
-- nearby bottleneck. Result is a plan, not automatic wall construction.
function planBaseDefenses(grid,center,halfWidth,canWall)
 local function key(x,y)return x..':'..y end
 local cells,index={},{}
 for _,c in ipairs(grid.cells)do if c.open then cells[#cells+1]=c;index[key(c.gx,c.gy)]=#cells end end
 local graph={};local source=#cells*2+1;local sink=source+1
 for i=1,sink do graph[i]={}end
 local INF=10000000
 local function edge(a,b,cap)
  local f={to=b,cap=cap};local r={to=a,cap=0};f.reverse=r;r.reverse=f
  graph[a][#graph[a]+1]=f;graph[b][#graph[b]+1]=r
 end
 local dirs={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}}
 local protected=0
 for i,c in ipairs(cells)do
  local extent=math.max(math.abs(c.x-center.x),math.abs(c.y-center.y))
  local inside=extent<=halfWidth
  local cost=inside and INF or (canWall(c) and (10+math.abs(extent-halfWidth-grid.step*2)/grid.step) or INF)
  edge(i*2-1,i*2,cost)
  if inside then edge(source,i*2-1,INF);protected=protected+1 end
  -- Threats are all distant open ground, not only the survey's outer row.
  -- Map edges/cliffs can make that row closed and leave one tiny exit,
  -- falsely suggesting a single hub defends the entire base.
  if extent>=halfWidth+grid.step*4 or math.abs(c.gx)==grid.n or math.abs(c.gy)==grid.n then edge(i*2,sink,INF)end
  for _,d in ipairs(dirs)do local j=index[key(c.gx+d[1],c.gy+d[2])];if j then edge(i*2,j*2-1,INF)end end
 end
 if protected==0 then return {cells={},lines={},error='No traversable base area found in survey; choose a different center'} end
 -- Dinic max-flow; the residual source side defines the protected region.
 local total=0
 while true do
  local level={[source]=0};local queue={source};local head=1
  while head<=#queue do
   local u=queue[head];head=head+1
   for _,e in ipairs(graph[u])do if e.cap>1e-7 and not level[e.to]then level[e.to]=level[u]+1;queue[#queue+1]=e.to end end
  end
  if not level[sink]then break end
  local next={}
  local function push(u,flow)
   if u==sink then return flow end
   next[u]=next[u]or 1
   while next[u]<=#graph[u]do
    local e=graph[u][next[u]]
    if e.cap>1e-7 and level[e.to]==level[u]+1 then
     local sent=push(e.to,math.min(flow,e.cap))
     if sent>1e-7 then e.cap=e.cap-sent;e.reverse.cap=e.reverse.cap+sent;return sent end
    end
    next[u]=next[u]+1
   end
   return 0
  end
  while true do local flow=push(source,INF);if flow==0 then break end;total=total+flow;if total>=INF then return {cells={},lines={},error='No legal wall separator within survey area'}end end
 end
 local seen={[source]=true};local queue={source};local head=1
 while head<=#queue do local u=queue[head];head=head+1;for _,e in ipairs(graph[u])do if e.cap>1e-7 and not seen[e.to]then seen[e.to]=true;queue[#queue+1]=e.to end end end
 local cut,cutIndex={},{}
 for i,c in ipairs(cells)do if seen[i*2-1]and not seen[i*2]then cut[#cut+1]=c;cutIndex[key(c.gx,c.gy)]=c end end
 -- Keep the cut cells as inspectable planned lines. They can form several
 -- separated closures where cliffs or structures already supply the rest.
 local lines={};local used={}
 for _,c in ipairs(cut)do
  if not used[key(c.gx,c.gy)]then
   local line={};local pending={c};used[key(c.gx,c.gy)]=true;local h=1
   while h<=#pending do local p=pending[h];h=h+1;line[#line+1]={x=p.x,y=p.y}
    for _,d in ipairs(dirs)do local k=key(p.gx+d[1],p.gy+d[2]);if cutIndex[k]and not used[k]then used[k]=true;pending[#pending+1]=cutIndex[k]end end
   end
   lines[#lines+1]=line
  end
 end
 return {cells=cut,lines=lines,cost=total,step=grid.step}
end
