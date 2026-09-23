# Run with julia --project=../chartroom scripts/export_julia_fixtures.jl OUTPUT
using Chartroom, Dates
const C=Chartroom
bars=[C.Bar(DateTime(2025,1,6)+Hour(i),100+i/10+sin(i),103+i/10+sin(i),97+i/10+sin(i),101+i/10+sin(i),1000+i) for i in 0:399]
h=C.History("SPY","USD","TEST","1d",bars,DateTime(2025,2,1),0)
mktempdir() do dir
    C.save_history(dir,h)
    history=C.JSON3.read(read(C.cache_path(dir,"SPY","1d"),String),Dict{String,Any})
    tests=[]
    for (kind,_) in C.INDICATOR_NAMES
        spec=C.Indicator(kind);result=C.calculate_indicator(spec,bars)
        push!(tests,Dict("spec"=>C.indicator_document(spec),"lines"=>[[isfinite(v) ? v : nothing for v in line] for line in result.lines],"scores"=>result.scores))
    end
    open(ARGS[1],"w") do io
        C.JSON3.write(io,Dict("history"=>history,"indicators"=>tests))
    end
end
