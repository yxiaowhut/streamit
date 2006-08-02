package at.dms.kjc.flatgraph;

import at.dms.kjc.sir.*;
import at.dms.kjc.*;
import java.util.*;


/**
 * This class will convert the FlatGraph back to an SIR representation.  
 * It will create the necessary SIR containers to represent the flat graph.  
 * This assume that the Flat graph is amenable to this conversion.  For example, 
 * it will not work if filter nodes have multiple input/output.   
 * 
 * 
 * @author mgordon
 */
public class FlatGraphToSIR
{
    /** The toplevel FlatNode of the flatgraph, the entry point */
    private FlatNode toplevel;
    /** The toplevel SIRPipeline for the SIR representation of the Flat graph */
    private SIRPipeline toplevelSIR;
    /** int used to name new containers when converting */
    private int id;
    /** unique int used to identity the toplevel pipeline*/
    private static int globalID;
    /** feedbacklops that need closing. */
   
    /**
     * Create a new FlatGraphToSIR with top as the entry-point
     * to the FlatGraph and construct an SIR graph from the flat graph.
     * 
     * Note: this expects splitters and joiners for a feedbackloop to
     * be in the same flatgraph.  Can we guarantee this in presence of
     * dynamic rates?  If not, what to do?
     * 
     * @param top The entry point to the Flat graph.
     */
    public FlatGraphToSIR(FlatNode top) 
    {
        toplevel = top;
        id = 0;


        toplevelSIR = new SIRPipeline("TopLevel" + globalID++);
        reSIR(toplevelSIR, toplevel, new HashSet<FlatNode>());
    }

    /**
     * Return the toplevel pipeline for the constructed SIR graph.
     * 
     * @return the toplevel pipeline for the constructed SIR graph.
     */
    public SIRPipeline getTopLevelSIR() 
    {
        assert toplevelSIR != null;
       
        return toplevelSIR;
    }
    
    /**
     * This recursive function will convert the flat graph into an 
     * SIR graph, creating the necessary containers along the way.
     * 
     * @param parent The current container.
     * @param current The node that we are about to work on.
     * @param visited The set of FlatNode we have already added to the SIR graph.
     */
    private void reSIR(SIRContainer parent, FlatNode current, 
            Set<FlatNode> visited) {

        // for filters: keep adding them to current pipeline until something 
        // other than a filter is found.
        // if no current pipeline, start a new pipeline.
        if (current.isFilter()) {
            SIRPipeline pipeline;

            if (!(parent instanceof SIRPipeline)) {
                pipeline = new SIRPipeline(parent, "Pipeline" + id++);
                pipeline.setInit(SIRStream.makeEmptyInit());
                parent.add(pipeline);
            }
            else {
                pipeline = (SIRPipeline)parent;
            }
            //keep adding filters to this pipeline
            while (current.isFilter()) {
                //make sure we have not added this filter before
                assert !(visited.contains(current)) : "Same filter found twice" + current.toString();
                //record that we have added this filter
                visited.add(current);
                List<JExpression> params;
                //if its parent does not contain it, just pass null params...
                if (parent.indexOf(current.getFilter()) == -1) {
                    params = new LinkedList<JExpression>(); 
                } else {
                    params = ((SIRFilter)current.contents).getParams();
                }
                pipeline.add((SIRFilter)current.contents, params);
                ((SIRFilter)current.contents).setParent(parent);
                //no outgoing edges: end pipeline.
                if (current.ways == 0)
                    return;
                //assert that there is exactly one outgoing edge of this filter
                assert current.ways == 1 && current.edges.length == 1;
                current = current.edges[0];
            }
        } 
        
        // current is not filter, either on entry to reSIR, or after processing
        // a number of filters.
        if (current.isSplitter()) {
          // splitter: 2 cases original parent should tell us
          // if this is a top of a splitjoin or bottom of a feedbackloop

          if (((SIRSplitter)(current.contents)).getParent() instanceof SIRSplitJoin) {  

            // Splitjoin splitter
            // create splitjoin with dummy joiner and recur on branches of split.
            SIRSplitJoin splitJoin = new SIRSplitJoin(parent, "SplitJoin" + id++);
            splitJoin.setInit(SIRStream.makeEmptyInit());
            //add this splitjoin to the parent!
            parent.add(splitJoin);
            //make sure we have not seen this splitter
            assert !(visited.contains(current)) : "Should not have encountered splitjoin splitter twice.";
            //record that we have added this splitter
            visited.add(current);
            splitJoin.setSplitter((SIRSplitter)current.contents);
            //create a dummy joiner for this split join just in case it does not have one
            SIRJoiner joiner = SIRJoiner.create(splitJoin, SIRJoinType.NULL, 
                                                ((SIRSplitter)current.contents).getWays());
            splitJoin.setJoiner(joiner);

            for (int i = 0; i < current.edges.length; i++) {
                if (current.edges[i] != null) {
                    //wrap all parallel splitter edges in a pipeline
                    SIRPipeline pipeline = new SIRPipeline(splitJoin, "Pipeline" + id++);
                    pipeline.setInit(SIRStream.makeEmptyInit());
                    splitJoin.add(pipeline);
                    reSIR(pipeline, current.edges[i], visited);
                }
        
            }
            return;
          } else {
              assert ((SIRSplitter)(current.contents)).getParent() instanceof SIRFeedbackLoop;
              // splitter of existing feedback loop.
              // bubble up to closest FeedbackLoop parent.
              SIRContainer fbloop = parent;
              while (!(fbloop instanceof SIRFeedbackLoop)) {
                  fbloop = fbloop.getParent();
              }
              assert fbloop != null;
              assert ! visited.contains(current);
              
              // record that we have added this splitter.
              visited.add(current);
              ((SIRFeedbackLoop)fbloop).setSplitter((SIRSplitter)current.contents);
              assert current.ways == 2 && current.edges[1] != null;
              SIRPipeline pipeline = new SIRPipeline(fbloop, "Pipeline" + id++);
              ((SIRFeedbackLoop)fbloop).setLoop(pipeline);
              reSIR(pipeline,current.edges[1],visited);
              reSIR(((SIRFeedbackLoop)fbloop).getParent(), current.edges[0], visited);
          }
          
        } else if (current.isJoiner()) {
            if (current.isFeedbackJoiner()) {
                if (! (visited.contains(current))) {
                    visited.add(current);
                    //new feedback loop.  Assume that splitter is in same graph.
                    // Set both up now.
                    SIRFeedbackLoop fbloop = new SIRFeedbackLoop(parent,"FBLoop" + id++);
                    SIRFeedbackLoop oldparent = (SIRFeedbackLoop)current.contents.getParent();
                    fbloop.setInit(SIRStream.makeEmptyInit());
                    fbloop.setDelay(oldparent.getDelay());
                    fbloop.setInitPath(oldparent.getInitPath());
                    fbloop.setJoiner((SIRJoiner)current.contents);
                    parent.add(fbloop);
                    assert current.ways == 1 && current.edges[0] != null;
                    SIRPipeline pipeline = new SIRPipeline(fbloop, "Pipeline" + id++);
                    fbloop.setBody(pipeline);
                    reSIR(pipeline, current.edges[0], visited);
                } // else end this recursion at end of loop portion
            } else {
              // end of existing splitjoin: update joiner and
              // reSIR continuation.
              if (! visited.contains(current)) {
                SIRContainer splitJoin = parent;
                while (!(splitJoin instanceof SIRSplitJoin)) {
                    splitJoin = splitJoin.getParent();
                }
                assert splitJoin != null;

                // record that we have added this Joiner
                visited.add(current);
                // first time we are seeing this joiner so set it as the
                // joiner and visit the remainder of the graph
                ((SIRSplitJoin) splitJoin).setJoiner((SIRJoiner) current.contents);
                if (current.ways > 0) {
                    assert current.edges.length == 1 && current.ways == 1
                    && current.edges[0] != null;
                    reSIR(((SIRSplitJoin) splitJoin).getParent(),
                            current.edges[0], visited);
                }
              }
            }

        } else {
            assert current == null;       // not filter, splitter, joiner: terminate recursion.
        }
        return;
    }
}
