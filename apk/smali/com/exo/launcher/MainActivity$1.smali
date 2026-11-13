.class Lcom/exo/launcher/MainActivity$1;
.super Ljava/lang/Object;
.implements Landroid/view/View$OnClickListener;
.field final synthetic this$0:Lcom/exo/launcher/MainActivity;
.method constructor <init>(Lcom/exo/launcher/MainActivity;)V
    .registers 2
    invoke-direct {p0}, Ljava/lang/Object;-><init>()V
    iput-object p1, p0, Lcom/exo/launcher/MainActivity$1;->this$0:Lcom/exo/launcher/MainActivity;
    return-void
.end method
.method public onClick(Landroid/view/View;)V
    .registers 3
    iget-object v0, p0, Lcom/exo/launcher/MainActivity$1;->this$0:Lcom/exo/launcher/MainActivity;
    new-instance v1, Lcom/exo/launcher/InjectThread;
    invoke-direct {v1, v0}, Lcom/exo/launcher/InjectThread;-><init>(Lcom/exo/launcher/MainActivity;)V
    invoke-virtual {v1}, Ljava/lang/Thread;->start()V
    return-void
.end method
