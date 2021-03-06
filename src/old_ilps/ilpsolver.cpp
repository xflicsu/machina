/*
 * ilpsolver.cpp
 *
 *  Created on: 2-feb-2016
 *      Author: M. El-Kebir
 */

#include "ilpsolver.h"
#include <lemon/time_measure.h>

IlpSolver::IlpSolver(const CloneTree& T,
                     const std::string& primary,
                     MigrationGraph::Pattern pattern,
                     const std::string& gurobiLogFilename,
                     const StringPairList& forcedComigrations)
  : _T(T)
  , _primary(primary)
  , _pattern(pattern)
  , _forcedComigrations(forcedComigrations)
  , _indexToArc()
  , _pArcToIndex(NULL)
  , _indexToNode()
  , _pNodeToIndex(NULL)
  , _anatomicalSiteToIndex()
  , _indexToAnatomicalSite()
  , _primaryIndex(-1)
  , _env(gurobiLogFilename)
  , _model(_env)
  , _x()
  , _y()
  , _z()
  , _c()
  , _pLPlus(NULL)
  , _LB(-1)
  , _UB(-1)
{
}

void IlpSolver::init(const IntTriple& bounds)
{
  initIndices();
  initVariables();
  initConstraints();
  initLeafConstraints();
  initForcedComigrations();
  initObjective(bounds);
  _model.update();
}

void IlpSolver::initIndices()
{
  const Digraph& T = tree();
  _pLPlus = new StringNodeMap(T);
  _pArcToIndex = new IntArcMap(T);
  _pNodeToIndex = new IntNodeMap(T);
  
  _indexToArc.clear();
  for (ArcIt a(T); a != lemon::INVALID; ++a)
  {
    _pArcToIndex->set(a, _indexToArc.size());
    _indexToArc.push_back(a);
  }
  
  _indexToNode.clear();
  for (NodeIt v(T); v != lemon::INVALID; ++v)
  {
    _pNodeToIndex->set(v, _indexToNode.size());
    _indexToNode.push_back(v);
  }
  
  _primaryIndex = -1;
  _anatomicalSiteToIndex.clear();
  _indexToAnatomicalSite.clear();
  const StringSet Sigma = _T.getAnatomicalSites();
  for (const std::string& s : Sigma)
  {
    _anatomicalSiteToIndex[s] = _indexToAnatomicalSite.size();
    _indexToAnatomicalSite.push_back(s);
    if (_primary == s)
    {
      _primaryIndex = _indexToAnatomicalSite.size() - 1;
    }
  }
  
  assert(_primaryIndex != -1);
}

void IlpSolver::initVariables()
{
  const Digraph& T = tree();
  
  const int nrArcs = _indexToArc.size();
  const int nrNodes = _indexToNode.size();
  const int nrAnatomicalSites = _indexToAnatomicalSite.size();
  
  char buf[1024];
  _x = VarMatrix(nrNodes);
  for (int i = 0; i < nrNodes; ++i)
  {
    _x[i] = VarArray(nrAnatomicalSites);
    for (int s = 0; s < nrAnatomicalSites; ++s)
    {
      snprintf(buf, 1024, "x_%s_%s",
               label(_indexToNode[i]).c_str(),
               _indexToAnatomicalSite[s].c_str());
      _x[i][s] = strlen(buf) > 255 ? _model.addVar(0, 1, 0, GRB_BINARY) : _model.addVar(0, 1, 0, GRB_BINARY, buf);
    }
  }
  
  _y = VarArray(nrArcs);
  for (int ij = 0; ij < nrArcs; ++ij)
  {
    Arc a_ij = _indexToArc[ij];
    Node v_i = T.source(a_ij);
    Node v_j = T.target(a_ij);
    
    snprintf(buf, 1024, "y_%s_%s",
             label(v_i).c_str(),
             label(v_j).c_str());
    _y[ij] = strlen(buf) > 255 ? _model.addVar(0, 1, 0, GRB_CONTINUOUS) : _model.addVar(0, 1, 0, GRB_CONTINUOUS, buf);
  }
  
  _z = VarMatrix(nrArcs);
  for (int ij = 0; ij < nrArcs; ++ij)
  {
    Arc a_ij = _indexToArc[ij];
    Node v_i = T.source(a_ij);
    Node v_j = T.target(a_ij);
    
    _z[ij] = VarArray(nrAnatomicalSites);
    for (int s = 0; s < nrAnatomicalSites; ++s)
    {
      snprintf(buf, 1024, "z_%s_%s_%s",
               label(v_i).c_str(),
               label(v_j).c_str(),
               _indexToAnatomicalSite[s].c_str());
      _z[ij][s] = strlen(buf) > 255 ? _model.addVar(0, 1, 0, GRB_BINARY) : _model.addVar(0, 1, 0, GRB_BINARY, buf);
    }
  }
  
  _c = VarMatrix(nrAnatomicalSites);
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
    _c[s] = VarArray(nrAnatomicalSites);
    for (int t = 0; t < nrAnatomicalSites; ++t)
    {
      snprintf(buf, 1024, "c_%s_%s",
               _indexToAnatomicalSite[s].c_str(),
               _indexToAnatomicalSite[t].c_str());
      _c[s][t] = _model.addVar(0, 1, 0, GRB_CONTINUOUS, buf);
    }
  }
  
  _d = VarArray(nrAnatomicalSites);
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
    snprintf(buf, 1024, "d_%s",
             _indexToAnatomicalSite[s].c_str());
    _d[s] = _model.addVar(0, 1, 0, GRB_CONTINUOUS, buf);
  }
  
  _model.update();
}

void IlpSolver::initLeafConstraints()
{
  // Leaf color
  const Digraph& T = tree();
  
  for (NodeIt v_i(T); v_i != lemon::INVALID; ++v_i)
  {
    if (!isLeaf(v_i))
    {
      continue;
    }
    
    int i = (*_pNodeToIndex)[v_i];
    assert(_anatomicalSiteToIndex.count(l(v_i)) == 1);
    int s = _anatomicalSiteToIndex[l(v_i)];
    
    _model.addConstr(_x[i][s] == 1);
  }
}

void IlpSolver::initConstraints()
{
  const Digraph& T = tree();
  
  const int nrArcs = _indexToArc.size();
  const int nrNodes = _indexToNode.size();
  const int nrAnatomicalSites = _indexToAnatomicalSite.size();
  
  GRBLinExpr sum;

  // Unique color
  for (int i = 0; i < nrNodes; ++i)
  {
    for (int s = 0; s < nrAnatomicalSites; ++s)
    {
      sum += _x[i][s];
    }
    _model.addConstr(sum == 1);
    sum.clear();
  }

  // Matching colors
  for (int ij = 0; ij < nrArcs; ++ij)
  {
    Arc a_ij = _indexToArc[ij];
    Node v_i = T.source(a_ij);
    Node v_j = T.target(a_ij);
    int i = (*_pNodeToIndex)[v_i];
    int j = (*_pNodeToIndex)[v_j];
    
    for (int s = 0; s < nrAnatomicalSites; ++s)
    {
      sum += _z[ij][s];
      _model.addConstr(_z[ij][s] <= _x[i][s]);
      _model.addConstr(_z[ij][s] <= _x[j][s]);
      //_model.addConstr(_z[ij][s] >= _x[i][s] + _x[j][s] - 1);
    }
    addMatchingColorsConstraint(sum, ij);
    
    sum.clear();
  }
  
  // Root color
  _model.addConstr(_x[(*_pNodeToIndex)[root()]][_primaryIndex] == 1);
  
  // Comigration constraints
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
    for (int t = 0; t < nrAnatomicalSites; ++t)
    {
      if (t == s)
      {
        _model.addConstr(_c[s][t] == 0);
      }
      else
      {
        for (int ij = 0; ij < nrArcs; ++ij)
        {
          Arc a_ij = _indexToArc[ij];
          Node v_i = T.source(a_ij);
          Node v_j = T.target(a_ij);
          int i = (*_pNodeToIndex)[v_i];
          int j = (*_pNodeToIndex)[v_j];
          
          addComigrationConstraint(s, t, ij, i, j);
        }
      }
    }
  }
  
  if (nrAnatomicalSites > 1)
  {
    _model.addConstr(_d[_primaryIndex] == 1);
  }
  
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
    for (int t = 0; t < nrAnatomicalSites; ++t)
    {
      _model.addConstr(_d[s] >= _c[s][t]);
    }
  }
  
  if (_pattern != MigrationGraph::R)
  {
    for (OutArcIt a_ij(T, root()); a_ij != lemon::INVALID; ++a_ij)
    {
      Node v_j = T.target(a_ij);
      int j = (*_pNodeToIndex)[v_j];
      sum += _x[j][_primaryIndex];
    }
    _model.addConstr(sum >= 1);
    sum.clear();
    
    for (NodeIt v_j(T); v_j != lemon::INVALID; ++v_j)
    {
      if (isLeaf(v_j))
      {
        assert(_anatomicalSiteToIndex.count(l(v_j)) == 1);
        int s = _anatomicalSiteToIndex[l(v_j)];
        if (s == _primaryIndex)
        {
          initPrimaryConstraint(v_j);
        }
      }
    }
  }
  
  if (_pattern == MigrationGraph::S || _pattern == MigrationGraph::PS)
  {
    initSingleSourceSeedingConstraints();
    if (_pattern == MigrationGraph::PS)
    {
      initParallelSingleSourceSeedingConstraints();
    }
  }
  else if (_pattern == MigrationGraph::M)
  {
    initMultiSourceSeedingConstraints();
  }
}

void IlpSolver::initPrimaryConstraint(Node v_j)
{
  const Digraph& T = tree();
  GRBLinExpr sum;
  
  for (InArcIt a_ij(T, v_j); a_ij != lemon::INVALID; ++a_ij)
  {
    Node v_i = T.source(a_ij);
    int i = (*_pNodeToIndex)[v_i];
    sum += _x[i][_primaryIndex];
  }
  
  if (sum.size() > 0)
  {
    _model.addConstr(sum >= 1);
    if (sum.size() == 1)
    {
      Node v_i = T.source(InArcIt(T, v_j));
      initPrimaryConstraint(v_i);
    }
  }
}

void IlpSolver::initMultiSourceSeedingConstraints()
{
  const int nrAnatomicalSites = _indexToAnatomicalSite.size();
  assert(0 <= _primaryIndex && _primaryIndex < nrAnatomicalSites);
  
  IntVector mets;
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
    if (s != _primaryIndex)
    {
      mets.push_back(s);
    }
  }
  
  GRBLinExpr sum;
  
  assert(mets.size() == nrAnatomicalSites - 1);
  BoolVector subset(mets.size(), false);
  while (next(subset))
  {
    IntVector selectedMets;
    for (int i = 0; i < nrAnatomicalSites - 1; ++i)
    {
      if (subset[i])
        selectedMets.push_back(mets[i]);
    }
    
    if (selectedMets.size() < 2)
      continue;
    
    // make all permutations
    do
    {
      // add cycle inequality
      for (int i = 1; i < selectedMets.size(); ++i)
      {
        sum += _c[selectedMets[i-1]][selectedMets[i]];
      }
      _model.addConstr(sum <= selectedMets.size() - 1);
      // TODO: add back edge to close the cycle
      sum.clear();
    } while(std::next_permutation(selectedMets.begin(), selectedMets.end()));
  }
  
  // primary has no incoming edges
  for (int t = 0; t < nrAnatomicalSites; ++t)
  {
    sum += _c[t][_primaryIndex];
  }
  _model.addConstr(sum == 0);
  sum.clear();
}

bool IlpSolver::next(BoolVector& subset)
{
  int n = subset.size();
  for (int i = 0; i < n; ++i)
  {
    if (subset[i] == false)
    {
      subset[i] = true;
      for (int j = 0; j < i; ++j)
      {
        subset[j] = false;
      }
      return true;
    }
  }
  
  return false;
}

void IlpSolver::initSingleSourceSeedingConstraints()
{
  const int nrAnatomicalSites = _indexToAnatomicalSite.size();
  
  GRBLinExpr sum;
  
  // Comigration constraints
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
    if (s == _primaryIndex)
    {
      for (int t = 0; t < nrAnatomicalSites; ++t)
      {
        sum += _c[t][s];
      }
      _model.addConstr(sum == 0);
      sum.clear();
    }
    else
    {
      for (int t = 0; t < nrAnatomicalSites; ++t)
      {
        sum += _c[t][s];
      }
      _model.addConstr(sum == 1);
      sum.clear();
    }
  }
  
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
//    _model.addConstr(_e[s] == 0);
    for (int t = 0; t < nrAnatomicalSites; ++t)
    {
      sum += _c[s][t];
    }
  }

  _model.addConstr(sum == nrAnatomicalSites - 1);
  sum.clear();
}

void IlpSolver::initParallelSingleSourceSeedingConstraints()
{
  int nrAnatomicalSites = _indexToAnatomicalSite.size();
  
  for (int s = 0; s < nrAnatomicalSites; ++s)
  {
    if (s != _primaryIndex)
    {
      _model.addConstr(_c[_primaryIndex][s] == 1);
    }
  }
}

void IlpSolver::initObjective(const IntTriple& bounds)
{
  const int nrArcs = _indexToArc.size();
  const int nrAnatomicalSites = _indexToAnatomicalSite.size();

  GRBLinExpr obj, sum_migrations, sum_comigrations, sum_seeding_sites;
  // migrations
  for (int ij = 0; ij < nrArcs; ++ij)
  {
    obj += _y[ij];
    sum_migrations += _y[ij];
  }
  _model.addConstr(sum_migrations >= nrAnatomicalSites - 1);
  
  if (bounds.first != -1)
  {
    _model.addConstr(sum_migrations <= bounds.first);
  }
  
  if (_forcedComigrations.empty()
      || _pattern == MigrationGraph::M
      || _pattern == MigrationGraph::R)
  {
    // comigrations
    {
      const double f = 1. / (nrArcs + 1);
      for (int s = 0; s < nrAnatomicalSites; ++s)
      {
        for (int t = 0; t < nrAnatomicalSites; ++t)
        {
          obj += f * _c[s][t];
          sum_comigrations += _c[s][t];
        }
      }
    }
    _model.addConstr(sum_comigrations >= nrAnatomicalSites - 1);
    _model.addConstr(sum_migrations >= sum_comigrations);
    
    if (bounds.second.first != -1)
    {
      _model.addConstr(sum_comigrations <= bounds.second.first);
    }
    
    // seeding sites
    {
      const double g = (1. / (nrArcs + 1)) * (1. / (nrAnatomicalSites + 1));
      for (int s = 0; s < nrAnatomicalSites; ++s)
      {
        obj += g * _d[s];
        sum_seeding_sites += _d[s];
      }
    }
    
    if (bounds.second.second != -1)
    {
      _model.addConstr(sum_seeding_sites <= bounds.second.second);
    }
  }
  
  _model.setObjective(obj, GRB_MINIMIZE);
  _model.update();
}

void IlpSolver::initForcedComigrations()
{
  for (const StringPair& st : _forcedComigrations)
  {
    assert(_anatomicalSiteToIndex.count(st.first) == 1);
    assert(_anatomicalSiteToIndex.count(st.second) == 1);
    
    int s = _anatomicalSiteToIndex[st.first];
    int t = _anatomicalSiteToIndex[st.second];
    
    _model.addConstr(_c[s][t] == 1);
  }
}

void IlpSolver::processSolution()
{
  const int nrAnatomicalSites = _indexToAnatomicalSite.size();
  const Digraph& T = tree();

  for (NodeIt v_i(T); v_i != lemon::INVALID; ++v_i)
  {
    int i = (*_pNodeToIndex)[v_i];
    bool found = false;
    for (int s = 0; s < nrAnatomicalSites; ++s)
    {
      if (_x[i][s].get(GRB_DoubleAttr_X) >= 0.4)
      {
        _pLPlus->set(v_i, _indexToAnatomicalSite[s]);
        found = true;
        break;
      }
    }
    assert(found);
  }
  
  assert((*_pLPlus)[root()] == _primary);
}

void IlpSolver::exportModel(const std::string& filename)
{
  _model.write(filename);
}

bool IlpSolver::solve(int nrThreads, int timeLimit)
{
  try
  {
    if (nrThreads > 0)
    {
      _model.getEnv().set(GRB_IntParam_Threads, nrThreads);
    }
    if (timeLimit > 0)
    {
      _model.getEnv().set(GRB_DoubleParam_TimeLimit, timeLimit);
    }
    _model.getEnv().set(GRB_IntParam_LogToConsole, 0);
    _model.optimize();
    int status = _model.get(GRB_IntAttr_Status);
    if (status == GRB_OPTIMAL || status == GRB_SUBOPTIMAL)
    {
      _LB = _model.get(GRB_DoubleAttr_ObjBound);
      _UB = _model.get(GRB_DoubleAttr_ObjVal);
      processSolution();
      return true;
    }
    else if (status == GRB_INF_OR_UNBD)
    {
      std::cerr << "Model is infeasible or unbounded" << std::endl;
      return false;
    }
    else if (status == GRB_INFEASIBLE)
    {
//      std::cerr << "Model is infeasible" << std::endl;
//      _model.computeIIS();
//      _model.write("/tmp/model_IIS.ilp");
      return false;
    }
    else if (status == GRB_UNBOUNDED)
    {
      std::cerr << "Model is unbounded" << std::endl;
      return false;
    }
    else if (status == GRB_TIME_LIMIT)
    {
      _LB = _model.get(GRB_DoubleAttr_ObjBound);
      _UB = _model.get(GRB_DoubleAttr_ObjVal);
      if (_UB <= lemon::countNodes(_T.tree()) * _T.getNrAnatomicalSites())
      {
        processSolution();
        return true;
      }
      return false;
    }
  }
  catch (const GRBException& e)
  {
    std::cerr << "Error code = " << e.getErrorCode() << std::endl;
    std::cerr << e.getMessage() << std::endl;
    
    return false;
  }
  catch (...)
  {
    return false;
  }
  
  return true;
}

void IlpSolver::run(const CloneTree& T,
                    const std::string& primary,
                    const std::string& outputDirectory,
                    const StringToIntMap& colorMap,
                    MigrationGraph::Pattern pattern,
                    int nrThreads,
                    bool outputILP,
                    bool outputSearchGraph,
                    int timeLimit,
                    const IntTriple& bounds,
                    const StringPairList& forcedComigrations)
{
  char buf[1024];
  std::string filenameGurobiLog;
  std::string filenameSearchGraph;
  
  if (!outputDirectory.empty())
  {
    snprintf(buf, 1024, "%s/log-%s-%s.txt",
             outputDirectory.c_str(),
             primary.c_str(),
             MigrationGraph::getPatternString(pattern).c_str());
    
    filenameGurobiLog = buf;
    
    snprintf(buf, 1024, "%s/searchG-%s-%s.dot",
             outputDirectory.c_str(),
             primary.c_str(),
             MigrationGraph::getPatternString(pattern).c_str());
    
    filenameSearchGraph = buf;
  }
  
  IlpSolver solver(T,
                   primary,
                   pattern,
                   filenameGurobiLog,
                   forcedComigrations);
  solver.init(bounds);
  
  if (!outputDirectory.empty() && outputILP)
  {
    snprintf(buf, 1024, "%s/ilp-%s-%s.ilp",
             outputDirectory.c_str(),
             primary.c_str(),
             MigrationGraph::getPatternString(pattern).c_str());
    solver.exportModel(buf);
  }
  
  lemon::Timer timer;
  
  std::cerr << "With primary '" << primary << "', "
            << "allowed patterns: ("
            << MigrationGraph::getAllowedPatternsString(pattern) << ")"
            << " and no binarization: ";
  
  if (!solver.solve(nrThreads, timeLimit))
  {
    std::cerr << "No solution found" << std::endl;
    return;
  }
  
  MigrationGraph G(T, solver.lPlus());

  std::cerr << G.getNrMigrations() << " migrations, "
    << G.getNrComigrations(T, solver.lPlus()) << " comigrations and "
    << G.getNrSeedingSites() << " seeding sites";
  if (G.hasReseeding())
  {
    std::cerr << " including reseeding";
  }
  std::cerr << ". [LB, UB] = [" << solver.LB() << ", " << solver.UB() << "]. "
    << timer.realTime() << " seconds" << std::endl;
  
  if (!outputDirectory.empty())
  {
    snprintf(buf, 1024, "%s/T-%s-%s.dot",
             outputDirectory.c_str(),
             primary.c_str(),
             MigrationGraph::getPatternString(pattern).c_str());
    std::ofstream outT(buf);
    T.writeDOT(outT, solver.lPlus(), colorMap);
    outT.close();
    
    snprintf(buf, 1024, "%s/G-%s-%s.dot",
             outputDirectory.c_str(),
             primary.c_str(),
             MigrationGraph::getPatternString(pattern).c_str());
    std::ofstream outG(buf);
    G.writeDOT(outG, colorMap);
    outG.close();
    
    snprintf(buf, 1024, "%s/T-%s-%s.labeling",
             outputDirectory.c_str(),
             primary.c_str(),
             MigrationGraph::getPatternString(pattern).c_str());
    std::ofstream outLabeling(buf);
    T.writeVertexLabeling(outLabeling, solver.lPlus());
    outLabeling.close();
  }
}
