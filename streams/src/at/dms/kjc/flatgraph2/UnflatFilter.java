package at.dms.kjc.flatgraph2;

import at.dms.kjc.sir.*;

/**
 * Intermediate file used in (super) synch removal
 */
public class UnflatFilter {
    public SIRFilter filter;
    public int[] inWeights,outWeights;
    //IntList inWeights,outWeights;
    public UnflatEdge[] in;
    public UnflatEdge[][] out;
    private static int nullNum=0;
    private String name;
    
    UnflatFilter(SIRStream filter,int[] inWeights,int[] outWeights,UnflatEdge[] in,UnflatEdge[][] out) {
	this.filter=(SIRFilter)filter;
	this.inWeights=inWeights;
	this.outWeights=outWeights;
	this.in=in;
	this.out=out;
	if(filter!=null)
	    name=filter.getName();
	else
	    name=new Integer(nullNum++).toString();
	for(int i=0;i<in.length;i++) {
	    in[i].dest=this;
	    //in[i].destIndex=i;
	}
	for(int i=0;i<out.length;i++) {
	    UnflatEdge[] innerOut=out[i];
	    for(int j=0;j<innerOut.length;j++)
		innerOut[j].src=this;
	}
    }

    /*UnflatFilter(SIRStream filter,int[] inWeights,IntList outWeights,UnflatEdge[] in,UnflatEdge[][] out) {
      IntList newInWeights=null;
      if(inWeights.length>0) {
      newInWeights=new IntList(inWeights[0],null,null);
      IntList cur=newInWeights;
      for(int i=1;i<inWeights.length;i++) {
      cur.next=new IntList(inWeights[i],cur,null);
      cur=cur.next;
      }
      }
      this(filter,newInWeights,outWeights,in,out);
      }*/

    UnflatFilter(SIRStream filter) {
	this(filter,null,null,new UnflatEdge[0],new UnflatEdge[0][0]);
    }

    UnflatFilter(SIRStream filter,UnflatEdge in,UnflatEdge out) {
	this(filter,new int[]{1},new int[]{1},new UnflatEdge[]{in},new UnflatEdge[][]{new UnflatEdge[]{out}});
    }

    public String toString() {
	return name;
    }
}
